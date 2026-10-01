#pragma once 

#include <cstdint>
#include <vector>

namespace tamdb {

struct VoteRequest {
    uint64_t term;
    uint32_t candidate_id;
    uint64_t last_log_index;
    uint64_t last_log_term;
};

struct LogEntry {
    uint64_t term;
    uint64_t log_index;
    std::vector<float> input_vector;
};

struct AppendEntriesRequest {
    uint64_t leader_term;
    uint32_t leader_id;
    uint64_t prev_log_index;
    uint64_t prev_log_term;
    uint64_t leader_commit_index;
    std::vector<LogEntry> logs;
};

struct VoteResponse {
    uint64_t term;
    bool vote;
};

struct AppendEntriesResponse {
    uint64_t term; 
    bool success
};

}