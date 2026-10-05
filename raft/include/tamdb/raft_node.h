#pragma once

#include <cstdint>
#include <vector>
#include <chrono>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>

#include "tamdb/client/raft_client.h"
#include "tamdb/constants/raft_node_roles.h"
#include "tamdb/models/raft_models.h"

namespace tamdb {

/**
 * Core Raft consensus state machine.
 *
 * Manages leader election, term tracking, and vote handling for a single
 * shard group. One RaftNode instance per shard group the node participates in.
 * Pure logic — no network or gRPC dependency; the node layer handles RPCs.
 */
class RaftNode {

public:
    /**
     * Construct a Raft node with its ID and the IDs of all peers in the shard group.
     * Starts as FOLLOWER with a randomized election timeout (150-300ms).
     *
     * @param node_id    Unique identifier for this node.
     * @param peer_nodes IDs of all other nodes in this shard group (not including self).
     * @param raft_client_ptr Shared pointer to the transport layer for sending RPCs to peers.
     */
    RaftNode(const uint32_t node_id, const std::vector<uint32_t> peer_nodes, RaftClientPtr raft_client_ptr);

    /**
     * Handle an incoming vote request from a candidate.
     * Grants vote if: candidate's term >= ours, we haven't voted this term
     * (or already voted for this candidate), and candidate's log is at least
     * as up-to-date as ours. Resets election timer on vote grant.
     *
     * @param vote_req The incoming vote request from a candidate.
     * @return VoteResponse with current term and whether vote was granted.
     */
    VoteResponse handle_vote_request(const VoteRequest& vote_req);

    /**
     * Handle an incoming AppendEntries RPC from a leader.
     * Serves as both heartbeat and log replication. Resets election timer
     * on valid request. Rejects if leader's term is stale.
     *
     * @param append_entries_req The incoming request from a leader.
     * @return AppendEntriesResponse with current term and success status.
     */
    AppendEntriesResponse handle_append_entries(const AppendEntriesRequest& append_entries_req);
    
    ~RaftNode();

private:
    /** Reset the election timer to now + a fresh random timeout. */
    void reset_election_timer();

    /** Transition to CANDIDATE, increment term, vote for self, and request votes from all peers. */
    void start_election();

    /** Step down to FOLLOWER when a higher term is observed. Resets voted_for. */
    void higher_term_step_down(uint64_t term);

    /**
     * Record a vote for the given candidate. Resets election timer.
     *
     * @param candidate_id The node ID to vote for.
     */
    void vote(uint32_t candidate_id);

    /**
     * Apply committed log entries to the HNSW index.
     *
     * @param prev_commit_index_of_node The previous commit index before this batch.
     */
    void commit(uint64_t prev_commit_index_of_node);

    /**
     * Background thread loop. Sleeps until election timeout expires,
     * then triggers start_election(). Exits when _shutdown is set.
     */
    void initiate_election();

    /**
     * Broadcast vote requests to all peers via RaftClient.
     * Counts responses and becomes LEADER if majority grants.
     */
    void send_vote_requests();
    
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

    // Background Threads Constructs 
    std::thread _election_thread;
    std::mutex _election_mutex;
    std::condition_variable _election_cond_var;
    bool _election_timer_reset;
    std::atomic<bool> _shutdown;

    // TODO
    std::thread commit_thread;
    
    std::vector<LogEntry> _logs;
    const static uint32_t NONE = UINT32_MAX;

    /**
     * For random election timeout generation
     */
    std::mt19937 _rng;
    std::uniform_int_distribution<int> _rand_election_timeout;

    RaftClientPtr _raft_client_ptr;
}; 

typedef std::shared_ptr<RaftNode> RaftNodePtr;
}
