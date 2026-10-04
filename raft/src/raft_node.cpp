#include "tamdb/raft_node.h"

#include <random>
#include <algorithm>

namespace tamdb {

RaftNode::RaftNode(const uint32_t node_id, const std::vector<uint32_t> peer_nodes) 
            : _node_id(node_id),
            _peer_nodes(peer_nodes),
            _term(0),
            _node_role(RaftNodeRole::FOLLOWER),
            _leader_node(RaftNode::NONE),
            _voted_for(RaftNode::NONE),
            _commit_index(RaftNode::NONE),
            _rng(std::random_device{}()),
            _rand_election_timeout(150, 300),
            _election_timer_reset(false),
            _shutdown(false) {
    reset_election_timer();
    _election_thread = std::thread(&RaftNode::initiate_election, this);
}


RaftNode::~RaftNode() {
    std::unique_lock lock(_election_mutex);
    _shutdown = true;
    _election_cond_var.notify_one(); // wake up cv from sleep and exit
    lock.unlock(); // give up the mutex so that election thread can clean up
    _election_thread.join()
}

VoteResponse RaftNode::handle_vote_request(const VoteRequest& vote_req) {

    std::unique_lock lock(_election_mutex);

    // 1: leader is outdated
    if(vote_req.term < _term) {
        return VoteResponse {_term, false};
    }     

    // Update node's term
    if(vote_req.term > _term) {
        higher_term_step_down(vote_req.term);
    }

    bool is_candidate_eligible = false;
    
    uint64_t node_last_term = 0;
    uint64_t node_last_log_index = 0;
    if(_logs.size() > 0) {
        node_last_term = _logs.back().term;
        node_last_log_index = _logs.back().log_index;
    }

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
    
    std::unique_lock lock(_election_mutex);

    // 1: leader is outdated
    if(append_entries_req.leader_term < _term) {
        return AppendEntriesResponse{_term, false};
    }

    reset_election_timer();

    if(append_entries_req.leader_term > _term) {
        higher_term_step_down(append_entries_req.leader_term);
    }

    uint64_t node_last_term = 0;
    uint64_t node_last_log_index = 0;
    if(_logs.size() > 0) {
        node_last_term = _logs.back().term;
        node_last_log_index = _logs.back().log_index;
    }

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
        
        
        // commit(last_commit_index); // TODO Move this to background thread
    }
    
    return AppendEntriesResponse{_term, true};
}


void RaftNode::commit(uint64_t last_commit_index) {
    // TODO
}

void RaftNode::send_vote_requests() {

}

void RaftNode::vote(uint32_t candidate_id) {
    reset_election_timer();
    _voted_for = candidate_id;
    _node_role = RaftNodeRole::FOLLOWER;
}

void RaftNode::higher_term_step_down(uint64_t term) {
    _term = term;
    _voted_for = NONE;
    _node_role = RaftNodeRole::FOLLOWER;
}

void RaftNode::reset_election_timer () {
    _last_heartbeat = std::chrono::steady_clock::now();
    _election_timeout = std::chrono::milliseconds(_rand_election_timeout(_rng));
    _election_timer_reset = true;
    _election_cond_var.notify_one();

}

void RaftNode::initiate_election() {
    auto hold_election = [&]() {
        return (_last_heartbeat + _election_timeout <= std::chrono::steady_clock::now());
    };

    while(! _shutdown) {
        std::unique_lock lock(_election_mutex);
        if(hold_election()) {
            ++_term;
            _node_role = RaftNodeRole::CANDIDATE;
            _voted_for = _node_id;
            // TODO Send vote requests
            reset_election_timer();
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
}