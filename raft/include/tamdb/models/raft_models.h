#pragma once 

#include <cstdint>
#include <vector>

namespace tamdb {

/** Candidate's request for a vote during leader election. */
struct VoteRequest {
    uint64_t term;
    uint32_t candidate_id;
    uint64_t last_log_index;
    uint64_t last_log_term;
};

struct Datum {
    uint64_t vector_id;
    std::vector<float> input_vector;
};

/** A single entry in the Raft log — represents one replicated command. */
struct LogEntry {
    uint64_t term;
    uint64_t log_index;
    Datum datum;
};

/** Leader's request to replicate log entries and/or send a heartbeat. */
struct AppendEntriesRequest {
    uint64_t leader_term;
    uint32_t leader_id;
    uint64_t prev_log_index;
    uint64_t prev_log_term;
    uint64_t leader_commit_index;
    std::vector<LogEntry> logs;
};

/** Response to a vote request — includes current term and whether vote was granted. */
struct VoteResponse {
    uint64_t term;
    bool vote;
};

/** Response to an AppendEntries RPC — includes current term and success status. */
struct AppendEntriesResponse {
    uint64_t term; 
    bool success;
};

/**
 * 
 */
struct WriteRequest {
    uint64_t id;
    std::vector<float> input_vector;
};

/**
 * 
 */
struct WriteResponse {
    bool success;
    bool is_leader;
    uint32_t leader_id;
};

}
