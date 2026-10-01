#pragma once

#include <cstdint>
#include <vector>
#include <chrono>
#include <random>

#include "tamdb/constants/raft_node_roles.h"
#include "tamdb/models/raft_models.h"

namespace tamdb {

class RaftNode {

public:
    /**
     * 
     */
    RaftNode(const uint32_t node_id, const std::vector<uint32_t> peer_nodes);

    /**
     * 
     */
    VoteResponse handle_vote_request(const VoteRequest& vote_req);

    /**
     * 
     */
    AppendEntriesResponse handle_append_entries(const AppendEntriesRequest& append_entries_req);


private:
    /**
     * 
     */
    void reset_election_timer();

    /**
     * 
     */
    void start_election();
    
    // Node Related Fields 
    uint32_t _node_id;
    std::vector<uint32_t> _peer_nodes;
    RaftNodeRole _node_role;
    uint32_t _leader_node;

    // Election related fields 
    uint64_t _term;
    uint32_t _voted_for;
    uint64_t _commit_index;

    std::chrono::milliseconds _election_timeout;
    std::chrono::steady_clock::time_point _last_heartbeat;
    
    std::vector<LogEntry> _logs;
    const static uint32_t NONE = UINT32_MAX;

    /**
     * For random election timeout generation
     */
    std::mt19937 _rng;
    std::uniform_int_distribution<int> _rand_election_timeout;
}; 
}