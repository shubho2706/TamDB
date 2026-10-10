#include "tamdb/raft_node.h"

#include <random>
#include <algorithm>
#include <iostream>
namespace tamdb {

RaftNode::RaftNode(const uint32_t node_id, const uint32_t peer_count, RaftOutboundClientPtr raft_client_ptr)
            : _node_id(node_id),
            _peer_count(peer_count),
            _term(0),
            _node_role(RaftNodeRole::FOLLOWER),
            _leader_node(RaftNode::NONE),
            _voted_for(RaftNode::NONE),
            _commit_index(RaftNode::NONE),
            _rng(std::random_device{}()),
            _rand_election_timeout(5000, 10000),
            _LEADER_HB_INTERVAL(std::chrono::milliseconds {300}),
            _election_timer_reset(false),
            _shutdown(false),
            _raft_outbound_client_ptr(std::move(raft_client_ptr)) {
    reset_tick_timer();
    _election_thread = std::thread(&RaftNode::tick, this);
}


RaftNode::~RaftNode() {
    std::unique_lock lock(_raft_mutex);
    _shutdown = true;
    _election_cond_var.notify_one(); // wake up cv from sleep and exit
    lock.unlock(); // give up the mutex so that election thread can clean up
    _election_thread.join();
}

VoteResponse RaftNode::handle_vote_request(const VoteRequest& vote_req) {

    std::unique_lock lock(_raft_mutex);

    // 1: leader is outdated
    if(vote_req.term < _term) {
        return VoteResponse {_term, false};
    }     

    // Update node's term
    if(vote_req.term > _term) {
        higher_term_step_down(vote_req.term);
    }

    bool is_candidate_eligible = false;

    std::pair<uint64_t, uint64_t> term_index_pair = last_log_info();
    uint64_t node_last_term = term_index_pair.first;
    uint64_t node_last_log_index = term_index_pair.second;

    // if candidate has higher term or has same term with higher log index
    if(vote_req.last_log_term > node_last_term
            || ( vote_req.last_log_term == node_last_term 
                && vote_req.last_log_index >= node_last_log_index) ) {
        is_candidate_eligible = true;
    }

    // 2: if the new leader is eligible and node has voted for the same leader or none -> grant vote
    if(is_candidate_eligible && (_voted_for == NONE || _voted_for == vote_req.candidate_id)) {
        // Register vote and reset timer
        vote(vote_req.candidate_id);
        return VoteResponse {_term, true};
    }

    return VoteResponse {_term, false};
}

AppendEntriesResponse RaftNode::handle_append_entries(const AppendEntriesRequest& append_entries_req) {    
    std::cout << "[Node " << _node_id << "] Received heartbeat from leader ###  " << std::endl;
    std::unique_lock lock(_raft_mutex);

    // 1: leader is outdated
    if(append_entries_req.leader_term < _term) {
        return AppendEntriesResponse{_term, false};
    }

    reset_tick_timer();
    std::cout << "[Node " << _node_id << "] Received heartbeat from leader " 
                << append_entries_req.leader_id << " term=" << append_entries_req.leader_term << std::endl;
    
    // HB from a Higher or Equal to Term Leader
    _leader_node = append_entries_req.leader_id;
    _node_role = RaftNodeRole::FOLLOWER;

    if(append_entries_req.leader_term > _term) {
        higher_term_step_down(append_entries_req.leader_term);
    }

    std::pair<uint64_t, uint64_t> term_index_pair = last_log_info();
    uint64_t node_last_term = term_index_pair.first;
    uint64_t node_last_log_index = term_index_pair.second;

    // 2: Need to backtrack -> send false to leader and leader will send the backtracked logs
    if((node_last_log_index < append_entries_req.prev_log_index) 
        || ( append_entries_req.prev_log_index > 0 && append_entries_req.prev_log_index - 1 < _logs.size() 
            && _logs.at(append_entries_req.prev_log_index - 1).term != append_entries_req.prev_log_term)) {
            // Leader needs to backtrack
            // if leader's term is ahead or node has the leader's last log but from a diff term than the leader
        return AppendEntriesResponse{_term, false};
    }

    // 3 + 4 : Conflicting with new Entries -> Erase and Overwrite
    for(size_t idx = 0; idx < append_entries_req.logs.size(); ++idx) {
        LogEntry entry = append_entries_req.logs.at(idx);
        if(entry.log_index - 1 < _logs.size()) {
            if(entry.term != _logs.at(entry.log_index - 1).term) {
                _logs.erase(_logs.begin() + entry.log_index - 1, _logs.end());
                _logs.insert(_logs.end(), 
                            append_entries_req.logs.begin() + idx, 
                            append_entries_req.logs.end());
                break;        
            } 
        } else {
            _logs.insert(_logs.end(), 
                        append_entries_req.logs.begin() + idx, 
                        append_entries_req.logs.end());
            break;
        }
    }

    // 5: Update Commit Index
    if(append_entries_req.leader_commit_index > _commit_index) {
        size_t last_log_index = _logs.empty() ? 0 : _logs.back().log_index;
        uint64_t last_commit_index = _commit_index;
        _commit_index = std::min(append_entries_req.leader_commit_index, static_cast<uint64_t>(last_log_index));
        
        commit(last_commit_index); // TODO Move this to background thread
    }
    
    return AppendEntriesResponse{_term, true};
}

void RaftNode::tick() {
    auto timer_expired = [&]() {
        return (_last_heartbeat + _election_timeout <= std::chrono::steady_clock::now());
    };

    while(! _shutdown) {
        std::unique_lock lock(_raft_mutex);
        if(timer_expired()) {

            if(_node_role == RaftNodeRole::LEADER) {
                lock.unlock(); // unlock during the RPCs
                send_heartbeats();
                lock.lock();
                reset_tick_timer(_LEADER_HB_INTERVAL);
            } else {
                ++_term;
                _node_role = RaftNodeRole::CANDIDATE;
                _voted_for = _node_id;
                std::cout << "[Node " << _node_id << "] Starting election for term " << _term << std::endl;
                std::vector<VoteResponse> votes = send_vote_requests(lock);
                std::cout << "[Node " << _node_id << "] Got " << votes.size() << " vote responses, peer_count=" << _peer_count << std::endl;
                if(_node_role == RaftNodeRole::CANDIDATE && received_majority_votes(votes)) {
                    _node_role = RaftNodeRole::LEADER;
                    _leader_node = _node_id;
                    std::cout << "[Node " << _node_id << "] *** BECAME LEADER *** term=" << _term << std::endl;
                    reset_tick_timer(_LEADER_HB_INTERVAL);
                } else {
                    std::cout << "[Node " << _node_id << "] Lost election term=" << _term << std::endl;
                    _voted_for = NONE;
                    _node_role = RaftNodeRole::FOLLOWER;
                    reset_tick_timer();
                }
            }  
        } 

        std::chrono::steady_clock::duration remaining_time = _last_heartbeat + _election_timeout 
                                                            - std::chrono::steady_clock::now();
        if(remaining_time > std::chrono::steady_clock::duration::zero()) {
            _election_cond_var.wait_for(lock, remaining_time,
                                         [&]() {return _election_timer_reset || _shutdown;
                                                });
        }
        // CV ack's that timer was reset set it to false again
        _election_timer_reset = false;
    }
}

void RaftNode::send_heartbeats() {
    std::pair<uint64_t, uint64_t> term_index_pair = last_log_info();
    uint64_t node_last_term = term_index_pair.first;
    uint64_t node_last_log_index = term_index_pair.second;

    AppendEntriesRequest append_req = {
        _term,
        _node_id,
        node_last_log_index,
        node_last_term,
        _commit_index,
        {}
    };
    std::vector<AppendEntriesResponse> hb_responses = _raft_outbound_client_ptr->send_append_entries(append_req);
    
    for(AppendEntriesResponse& hp_resp : hb_responses) {
        if(hp_resp.term > _term) {
            // Step Down
            higher_term_step_down(hp_resp.term);
            break;
        }
    }
}

void RaftNode::commit(const uint64_t prev_commit_index_of_node) {
    for(uint64_t index = prev_commit_index_of_node - 1; index <= _commit_index; ++index) {
        // TODO: Apply the Index Write operation
    }
}

std::vector<VoteResponse> RaftNode::send_vote_requests(std::unique_lock<std::mutex>& lock) {
    std::pair<uint64_t, uint64_t> term_index_pair = last_log_info();
    uint64_t node_last_term = term_index_pair.first;
    uint64_t node_last_log_index = term_index_pair.second;

    VoteRequest vote_request = {_term, _node_id, node_last_log_index, node_last_term};
    lock.unlock();
    std::vector<VoteResponse> vote_responses = _raft_outbound_client_ptr->send_request_votes(vote_request);
    lock.lock();
    return vote_responses;
}

bool RaftNode::received_majority_votes(const std::vector<VoteResponse>& vote_responses) {
    uint32_t in_favour = 1, total = 1 + _peer_count; // register self vote
    for(const VoteResponse& vote_res: vote_responses) {
        if(vote_res.vote) ++in_favour;
    }

    int majority = (total / 2) + 1;
    return in_favour >= majority;
}

void RaftNode::vote(const uint32_t candidate_id) {
    reset_tick_timer();
    _voted_for = candidate_id;
    _node_role = RaftNodeRole::FOLLOWER;
}

void RaftNode::higher_term_step_down(const uint64_t term) {
    _term = term;
    _voted_for = NONE;
    _node_role = RaftNodeRole::FOLLOWER;
}

void RaftNode::reset_tick_timer (std::optional<std::chrono::milliseconds> timeout) {
    _last_heartbeat = std::chrono::steady_clock::now();
    _election_timeout = timeout.has_value() ? timeout.value()
                        : std::chrono::milliseconds(_rand_election_timeout(_rng));
    _election_timer_reset = true;
    _election_cond_var.notify_one();
}

std::pair<uint64_t, uint64_t> RaftNode::last_log_info() {
    if(_logs.empty()) {
        return std::pair {0, 0};
    }
    
    return std::pair {_logs.back().term, _logs.back().log_index};
}

WriteResponse RaftNode::handle_write_entries(WriteRequest& write_request) {
    std::unique_lock lock(_raft_mutex);
    
    if(_node_role != RaftNodeRole::LEADER) {
        // reject writes since node is not leader
        return {false, false, _leader_node};
    }

    uint64_t last_log_index = (_logs.empty()) ? 0: _logs.back().log_index;
    LogEntry log_entry = {  _term,
                            1 + last_log_index,
                            {   
                                write_request.id,
                                std::move(write_request.input_vector)
                            }
                        };
    _logs.push_back(std::move(log_entry));

    // TODO: Append Entries to Followers
    return {true, true, _leader_node};
    
}
}
