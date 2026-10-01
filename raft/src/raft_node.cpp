#include "tamdb/raft_node.h"
#include <random>

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
            _rand_election_timeout(150, 300) {
    reset_election_timer();
}



VoteResponse RaftNode::handle_vote_request(const VoteRequest& vote_req) {
    if(vote_req.term < _term) {
        return VoteResponse {_term, false};
    } 

    // Update node's term
    if(vote_req.term > _term) {
        _term = vote_req.term;
        _voted_for = NONE;
        _node_role = RaftNodeRole::FOLLOWER;
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

    if(is_candidate_eligible && (_voted_for == NONE || _voted_for == vote_req.candidate_id)) {
        
        // Register vote and reset timer
        _voted_for = vote_req.candidate_id;
        _node_role = RaftNodeRole::FOLLOWER;

        reset_election_timer();

        return VoteResponse {_term, true};
    }

    return VoteResponse {_term, false};
}

AppendEntriesResponse RaftNode::handle_append_entries(const AppendEntriesRequest& append_entries_req) {
    // TODO
    return AppendEntriesResponse{_term, false};
}


void RaftNode::reset_election_timer () {
    _last_heartbeat = std::chrono::steady_clock::now();
    _election_timeout = std::chrono::milliseconds(_rand_election_timeout(_rng));
}
}