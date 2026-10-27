#include "tamdb/raft_node.h"
#include "tamdb/client/raft_client.h"
#include "tamdb/commit/commit_state.h"
#include "tamdb/models/raft_models.h"
#include "tamdb/constants/raft_node_roles.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <functional>

namespace tamdb {
namespace {

// ============================================================================
// Test Infrastructure: CommitRecord (capture type for MockCommitHandler)
// ============================================================================

struct CommitRecord {
    uint64_t id;
    std::vector<float> input_vector;
};

// ============================================================================
// MockOutboundClient
// Configurable responses and request capture. Thread-safe via mock_mutex.
// append_callback (if set) overrides append_responses for dynamic behavior.
// ============================================================================

class MockOutboundClient : public IRaftOutboundClient {
public:
    std::vector<VoteResponse> vote_responses;
    std::vector<AppendEntriesResponse> append_responses;
    std::vector<VoteRequest> captured_vote_requests;
    std::vector<AppendEntriesRequest> captured_append_requests;
    std::function<std::vector<AppendEntriesResponse>(const AppendEntriesRequest&)> append_callback;
    std::mutex mock_mutex;

    std::vector<VoteResponse> send_request_votes(const VoteRequest& vote_req) override {
        std::lock_guard<std::mutex> lock(mock_mutex);
        captured_vote_requests.push_back(vote_req);
        return vote_responses;
    }

    std::vector<AppendEntriesResponse> send_append_entries(const AppendEntriesRequest& req) override {
        std::lock_guard<std::mutex> lock(mock_mutex);
        captured_append_requests.push_back(req);
        if (append_callback) {
            return append_callback(req);
        }
        return append_responses;
    }

    void clear_captures() {
        std::lock_guard<std::mutex> lock(mock_mutex);
        captured_vote_requests.clear();
        captured_append_requests.clear();
    }
};

// ============================================================================
// MockCommitHandler
// Captures all commit calls for assertion. Thread-safe via mock_mutex.
// ============================================================================

class MockCommitHandler : public IRaftCommitStateHandler {
public:
    std::vector<CommitRecord> captured_commits;
    bool should_fail = false;
    std::mutex mock_mutex;

    bool commit(uint64_t id, const std::vector<float>& input_vector) override {
        std::lock_guard<std::mutex> lock(mock_mutex);
        CommitRecord record;
        record.id = id;
        record.input_vector = input_vector;
        captured_commits.push_back(record);
        return !should_fail;
    }

    std::vector<CommitRecord> get_captures() {
        std::lock_guard<std::mutex> lock(mock_mutex);
        return captured_commits;
    }

    void clear_captures() {
        std::lock_guard<std::mutex> lock(mock_mutex);
        captured_commits.clear();
    }

    size_t capture_count() {
        std::lock_guard<std::mutex> lock(mock_mutex);
        return captured_commits.size();
    }
};

// ============================================================================
// Test Helpers
// ============================================================================

std::unique_ptr<RaftNode> make_node(
    uint32_t node_id,
    uint32_t peer_count,
    std::unique_ptr<MockOutboundClient> mock_client,
    std::unique_ptr<MockCommitHandler> mock_handler)
{
    return std::make_unique<RaftNode>(
        node_id, peer_count,
        std::move(mock_client),
        std::move(mock_handler));
}

// Drives the node's term upward by sending an AppendEntries from a fake leader.
void set_node_term(RaftNode& node, uint64_t term) {
    AppendEntriesRequest req;
    req.leader_term = term;
    req.leader_id = 99;
    req.prev_log_index = 0;
    req.prev_log_term = 0;
    req.leader_commit_index = 0;
    req.logs = {};
    node.handle_append_entries(req);
}

// Injects log entries at the given prev position. Returns the AE response.
AppendEntriesResponse inject_log_entries(
    RaftNode& node,
    uint64_t leader_term,
    uint64_t prev_log_index,
    uint64_t prev_log_term,
    std::vector<LogEntry> entries)
{
    AppendEntriesRequest req;
    req.leader_term = leader_term;
    req.leader_id = 99;
    req.prev_log_index = prev_log_index;
    req.prev_log_term = prev_log_term;
    req.leader_commit_index = 0;
    req.logs = std::move(entries);
    return node.handle_append_entries(req);
}

// Polls handle_write_entries until the node reports is_leader=true (up to 12s).
// The election timeout is 5-10s; tests using this helper are intentionally slow.
bool wait_for_leader(RaftNode& node) {
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        WriteRequest probe;
        probe.id = 9999;
        probe.input_vector = {0.0f};
        WriteResponse resp = node.handle_write_entries(probe);
        if (resp.is_leader) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Fixture: WriteEntriesTest
// Creates a 3-node cluster (peer_count=2) with a mock that grants election
// majority, then waits up to 12s for the node to self-elect. Each TEST_F
// instance is isolated: SetUp / TearDown run per test.
// NOTE: Each test in this suite incurs ~5-10s setup wait (election timeout).
// ============================================================================

class WriteEntriesTest : public ::testing::Test {
protected:
    MockOutboundClient* mock_client_raw;
    MockCommitHandler* mock_handler_raw;
    std::unique_ptr<RaftNode> node;

    void SetUp() override {
        std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
        std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();

        // 2 yes votes guarantees election majority in a 3-node cluster (self+2=3/3).
        client->vote_responses = {VoteResponse{0, true}, VoteResponse{0, true}};
        // Successful append responses for heartbeats during setup and probing.
        client->append_responses = {AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true}};

        mock_client_raw = client.get();
        mock_handler_raw = handler.get();

        node = make_node(1, 2, std::move(client), std::move(handler));

        bool leadership = wait_for_leader(*node);
        ASSERT_TRUE(leadership) << "Node did not become leader within 12s timeout";

        // Clear the commit captured by the leadership-probe write.
        mock_handler_raw->clear_captures();
    }

    void TearDown() override {
        node.reset();
    }
};

// ============================================================================
// Vote Request Tests
// ============================================================================

TEST(VoteRequestTest, StaleTermRejectsVote) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    set_node_term(node, 5);

    VoteRequest req = {3, 2, 0, 0};
    VoteResponse resp = node.handle_vote_request(req);

    EXPECT_EQ(resp.term, 5u);
    EXPECT_FALSE(resp.vote);
}

TEST(VoteRequestTest, HigherTermGrantsVote) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    set_node_term(node, 1);

    // Candidate at higher term with at least as up-to-date a log (empty).
    VoteRequest req = {3, 2, 0, 0};
    VoteResponse resp = node.handle_vote_request(req);

    EXPECT_TRUE(resp.vote);
    EXPECT_EQ(resp.term, 3u);
}

TEST(VoteRequestTest, EmptyLogGrantsEligibleCandidate) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    // Fresh node: term=0, empty log. Any candidate at term>=1 with empty log is eligible.
    VoteRequest req = {1, 2, 0, 0};
    VoteResponse resp = node.handle_vote_request(req);

    EXPECT_TRUE(resp.vote);
    EXPECT_EQ(resp.term, 1u);
}

TEST(VoteRequestTest, AlreadyVotedForSameCandidate) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    VoteRequest req1 = {1, 2, 0, 0};
    VoteResponse resp1 = node.handle_vote_request(req1);
    ASSERT_TRUE(resp1.vote);

    // Same candidate, same term — node should still grant.
    VoteRequest req2 = {1, 2, 0, 0};
    VoteResponse resp2 = node.handle_vote_request(req2);

    EXPECT_TRUE(resp2.vote);
    EXPECT_EQ(resp2.term, 1u);
}

TEST(VoteRequestTest, AlreadyVotedForDifferentCandidate) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    VoteRequest req1 = {1, 2, 0, 0};
    VoteResponse resp1 = node.handle_vote_request(req1);
    ASSERT_TRUE(resp1.vote);

    // Different candidate, same term — already voted for candidate 2.
    VoteRequest req2 = {1, 3, 0, 0};
    VoteResponse resp2 = node.handle_vote_request(req2);

    EXPECT_FALSE(resp2.vote);
    EXPECT_EQ(resp2.term, 1u);
}

TEST(VoteRequestTest, CandidateLogBehindRejects) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    // Node has one log entry at term 5 — last_log_term=5.
    std::vector<LogEntry> entries = {LogEntry{5, 1, Datum{1, {1.0f}}}};
    inject_log_entries(node, 5, 0, 0, entries);

    // Candidate at higher election term but log only at term 3 (behind node's term 5).
    VoteRequest req = {6, 2, 5, 3};
    VoteResponse resp = node.handle_vote_request(req);

    EXPECT_FALSE(resp.vote);
    EXPECT_EQ(resp.term, 6u);
}

TEST(VoteRequestTest, CandidateEqualTermHigherIndexGrants) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    // Build node log: 3 entries at term 2, so last_log = {term=2, index=3}.
    inject_log_entries(node, 2, 0, 0, {LogEntry{2, 1, Datum{1, {1.0f}}}});
    inject_log_entries(node, 2, 1, 2, {LogEntry{2, 2, Datum{2, {2.0f}}}});
    inject_log_entries(node, 2, 2, 2, {LogEntry{2, 3, Datum{3, {3.0f}}}});

    // Candidate: same last_log_term=2 but higher index=4 — more up-to-date.
    VoteRequest req = {3, 2, 4, 2};
    VoteResponse resp = node.handle_vote_request(req);

    EXPECT_TRUE(resp.vote);
    EXPECT_EQ(resp.term, 3u);
}

TEST(VoteRequestTest, CandidateEqualTermLowerIndexRejects) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    // Same setup: node last_log = {term=2, index=3}.
    inject_log_entries(node, 2, 0, 0, {LogEntry{2, 1, Datum{1, {1.0f}}}});
    inject_log_entries(node, 2, 1, 2, {LogEntry{2, 2, Datum{2, {2.0f}}}});
    inject_log_entries(node, 2, 2, 2, {LogEntry{2, 3, Datum{3, {3.0f}}}});

    // Candidate: same last_log_term=2 but lower index=2 — less up-to-date.
    VoteRequest req = {3, 2, 2, 2};
    VoteResponse resp = node.handle_vote_request(req);

    EXPECT_FALSE(resp.vote);
    EXPECT_EQ(resp.term, 3u);
}

// ============================================================================
// Append Entries Tests
// ============================================================================

TEST(AppendEntriesTest, StaleLeaderReject) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    set_node_term(node, 5);

    AppendEntriesRequest req;
    req.leader_term = 3;
    req.leader_id = 99;
    req.prev_log_index = 0;
    req.prev_log_term = 0;
    req.leader_commit_index = 0;
    req.logs = {};
    AppendEntriesResponse resp = node.handle_append_entries(req);

    EXPECT_EQ(resp.term, 5u);
    EXPECT_FALSE(resp.success);
}

TEST(AppendEntriesTest, ValidHeartbeatEmptyLogs) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    AppendEntriesRequest req;
    req.leader_term = 1;
    req.leader_id = 99;
    req.prev_log_index = 0;
    req.prev_log_term = 0;
    req.leader_commit_index = 0;
    req.logs = {};
    AppendEntriesResponse resp = node.handle_append_entries(req);

    EXPECT_EQ(resp.term, 1u);
    EXPECT_TRUE(resp.success);
}

TEST(AppendEntriesTest, HigherTermLeaderCausesStepDown) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    set_node_term(node, 1);

    AppendEntriesRequest req;
    req.leader_term = 3;
    req.leader_id = 99;
    req.prev_log_index = 0;
    req.prev_log_term = 0;
    req.leader_commit_index = 0;
    req.logs = {};
    AppendEntriesResponse resp = node.handle_append_entries(req);

    // Node should step down to term 3 and accept.
    EXPECT_EQ(resp.term, 3u);
    EXPECT_TRUE(resp.success);
}

TEST(AppendEntriesTest, NodeBehindLeaderReturnsFalse) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    // Node has 0 log entries; leader claims prev_log_index=5 — node must backtrack.
    AppendEntriesRequest req;
    req.leader_term = 1;
    req.leader_id = 99;
    req.prev_log_index = 5;
    req.prev_log_term = 1;
    req.leader_commit_index = 0;
    req.logs = {};
    AppendEntriesResponse resp = node.handle_append_entries(req);

    EXPECT_FALSE(resp.success);
}

TEST(AppendEntriesTest, TermMismatchReturnsFalse) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    // Node has entry at index=1 with term=1.
    inject_log_entries(node, 1, 0, 0, {LogEntry{1, 1, Datum{1, {1.0f}}}});

    // Leader claims prev_log_term=2 at index=1 — node has term=1 there.
    AppendEntriesRequest req;
    req.leader_term = 2;
    req.leader_id = 99;
    req.prev_log_index = 1;
    req.prev_log_term = 2;
    req.leader_commit_index = 0;
    req.logs = {};
    AppendEntriesResponse resp = node.handle_append_entries(req);

    EXPECT_FALSE(resp.success);
}

TEST(AppendEntriesTest, AppendNewEntries) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    MockCommitHandler* handler_raw = handler.get();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    std::vector<LogEntry> entries = {LogEntry{1, 1, Datum{100, {5.0f, 6.0f}}}};
    AppendEntriesResponse append_resp = inject_log_entries(node, 1, 0, 0, entries);

    EXPECT_TRUE(append_resp.success);

    // Commit the entry and verify via handler.
    AppendEntriesRequest commit_req;
    commit_req.leader_term = 1;
    commit_req.leader_id = 99;
    commit_req.prev_log_index = 1;
    commit_req.prev_log_term = 1;
    commit_req.leader_commit_index = 1;
    commit_req.logs = {};
    AppendEntriesResponse commit_resp = node.handle_append_entries(commit_req);

    EXPECT_TRUE(commit_resp.success);

    std::vector<CommitRecord> captures = handler_raw->get_captures();
    ASSERT_EQ(captures.size(), 1u);
    EXPECT_EQ(captures[0].id, 100u);
    std::vector<float> expected = {5.0f, 6.0f};
    EXPECT_EQ(captures[0].input_vector, expected);
}

TEST(AppendEntriesTest, AppendMultipleEntries) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    std::vector<LogEntry> entries = {
        LogEntry{1, 1, Datum{1, {1.0f}}},
        LogEntry{1, 2, Datum{2, {2.0f}}},
        LogEntry{1, 3, Datum{3, {3.0f}}}
    };
    AppendEntriesResponse resp = inject_log_entries(node, 1, 0, 0, entries);

    EXPECT_TRUE(resp.success);

    // Verify all 3 entries were stored: leader heartbeat references index=3.
    AppendEntriesRequest verify_req;
    verify_req.leader_term = 1;
    verify_req.leader_id = 99;
    verify_req.prev_log_index = 3;
    verify_req.prev_log_term = 1;
    verify_req.leader_commit_index = 0;
    verify_req.logs = {};
    AppendEntriesResponse verify_resp = node.handle_append_entries(verify_req);

    EXPECT_TRUE(verify_resp.success);
}

TEST(AppendEntriesTest, ConflictingEntryErasesAndRewrites) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    // Build node log: [{term=1,idx=1}, {term=1,idx=2}].
    inject_log_entries(node, 1, 0, 0, {LogEntry{1, 1, Datum{1, {1.0f}}}});
    inject_log_entries(node, 1, 1, 1, {LogEntry{1, 2, Datum{2, {2.0f}}}});

    // New leader at term=3 sends entry at index=2 with term=3 — conflicts with term=1.
    AppendEntriesResponse resp = inject_log_entries(node, 3, 1, 1, {LogEntry{3, 2, Datum{99, {9.0f}}}});

    EXPECT_TRUE(resp.success);

    // Verify: node now accepts a heartbeat that claims prev at index=2, term=3.
    AppendEntriesRequest verify_req;
    verify_req.leader_term = 3;
    verify_req.leader_id = 99;
    verify_req.prev_log_index = 2;
    verify_req.prev_log_term = 3;
    verify_req.leader_commit_index = 0;
    verify_req.logs = {};
    AppendEntriesResponse verify_resp = node.handle_append_entries(verify_req);

    EXPECT_TRUE(verify_resp.success);
}

TEST(AppendEntriesTest, CommitIndexAdvances) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    MockCommitHandler* handler_raw = handler.get();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    inject_log_entries(node, 1, 0, 0, {LogEntry{1, 1, Datum{42, {7.0f}}}});

    // Leader commits at index=1.
    AppendEntriesRequest commit_req;
    commit_req.leader_term = 1;
    commit_req.leader_id = 99;
    commit_req.prev_log_index = 1;
    commit_req.prev_log_term = 1;
    commit_req.leader_commit_index = 1;
    commit_req.logs = {};
    AppendEntriesResponse resp = node.handle_append_entries(commit_req);

    EXPECT_TRUE(resp.success);

    std::vector<CommitRecord> captures = handler_raw->get_captures();
    ASSERT_EQ(captures.size(), 1u);
    EXPECT_EQ(captures[0].id, 42u);
    std::vector<float> expected = {7.0f};
    EXPECT_EQ(captures[0].input_vector, expected);
}

TEST(AppendEntriesTest, HeartbeatDoesNotCommitWhenAlreadyCaughtUp) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    MockCommitHandler* handler_raw = handler.get();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    inject_log_entries(node, 1, 0, 0, {LogEntry{1, 1, Datum{1, {1.0f}}}});

    AppendEntriesRequest commit_req;
    commit_req.leader_term = 1;
    commit_req.leader_id = 99;
    commit_req.prev_log_index = 1;
    commit_req.prev_log_term = 1;
    commit_req.leader_commit_index = 1;
    commit_req.logs = {};
    node.handle_append_entries(commit_req);

    size_t captures_before = handler_raw->capture_count();
    ASSERT_EQ(captures_before, 1u);

    // Repeated heartbeat with the same commit_index — must not re-commit.
    AppendEntriesRequest hb;
    hb.leader_term = 1;
    hb.leader_id = 99;
    hb.prev_log_index = 1;
    hb.prev_log_term = 1;
    hb.leader_commit_index = 1;
    hb.logs = {};
    node.handle_append_entries(hb);

    EXPECT_EQ(handler_raw->capture_count(), captures_before);
}

TEST(AppendEntriesTest, MultipleEntriesCommitted) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    MockCommitHandler* handler_raw = handler.get();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    std::vector<LogEntry> entries = {
        LogEntry{1, 1, Datum{10, {1.0f}}},
        LogEntry{1, 2, Datum{20, {2.0f}}},
        LogEntry{1, 3, Datum{30, {3.0f}}}
    };
    inject_log_entries(node, 1, 0, 0, entries);

    // Commit all 3 in one shot.
    AppendEntriesRequest commit_req;
    commit_req.leader_term = 1;
    commit_req.leader_id = 99;
    commit_req.prev_log_index = 3;
    commit_req.prev_log_term = 1;
    commit_req.leader_commit_index = 3;
    commit_req.logs = {};
    node.handle_append_entries(commit_req);

    std::vector<CommitRecord> captures = handler_raw->get_captures();
    ASSERT_EQ(captures.size(), 3u);
    EXPECT_EQ(captures[0].id, 10u);
    EXPECT_EQ(captures[1].id, 20u);
    EXPECT_EQ(captures[2].id, 30u);
}

// ============================================================================
// Write Entries Tests  (via WriteEntriesTest fixture)
// Each test waits ~5-10s in SetUp for leader election. This is by design.
// ============================================================================

TEST_F(WriteEntriesTest, RejectIfNotLeader) {
    // Create a separate follower — never given a chance to win election.
    std::unique_ptr<MockOutboundClient> follower_client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> follower_handler = std::make_unique<MockCommitHandler>();
    RaftNode follower(2, 2, std::move(follower_client), std::move(follower_handler));

    WriteRequest req;
    req.id = 1;
    req.input_vector = {1.0f, 2.0f};
    WriteResponse resp = follower.handle_write_entries(req);

    EXPECT_FALSE(resp.success);
    EXPECT_FALSE(resp.is_leader);
}

TEST_F(WriteEntriesTest, SuccessfulWriteWithMajority) {
    // 2 of 2 peers ack → self + 2 = 3/3 majority.
    mock_client_raw->append_responses = {AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true}};

    WriteRequest req;
    req.id = 1;
    req.input_vector = {1.0f, 2.0f};
    WriteResponse resp = node->handle_write_entries(req);

    EXPECT_TRUE(resp.success);
    EXPECT_TRUE(resp.is_leader);
    EXPECT_EQ(resp.leader_id, 1u);
}

TEST_F(WriteEntriesTest, WriteFailsWithoutMajority) {
    // 0 peer acks → self only = 1/3, below majority threshold of 2.
    mock_client_raw->append_responses = {};

    WriteRequest req;
    req.id = 2;
    req.input_vector = {3.0f, 4.0f};
    WriteResponse resp = node->handle_write_entries(req);

    EXPECT_FALSE(resp.success);
    EXPECT_TRUE(resp.is_leader);
}

TEST_F(WriteEntriesTest, WriteAppendsToLog) {
    mock_client_raw->append_responses = {AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true}};

    // Preserve a copy before the write moves req.input_vector into the log.
    std::vector<float> data = {5.0f, 6.0f, 7.0f};
    WriteRequest req;
    req.id = 42;
    req.input_vector = data;
    WriteResponse resp = node->handle_write_entries(req);

    EXPECT_TRUE(resp.success);

    std::vector<CommitRecord> captures = mock_handler_raw->get_captures();
    ASSERT_EQ(captures.size(), 1u);
    EXPECT_EQ(captures[0].id, 42u);
    std::vector<float> expected = {5.0f, 6.0f, 7.0f};
    EXPECT_EQ(captures[0].input_vector, expected);
}

TEST_F(WriteEntriesTest, MultipleSequentialWrites) {
    mock_client_raw->append_responses = {AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true}};

    WriteRequest req1;
    req1.id = 10;
    req1.input_vector = {1.0f};
    WriteResponse resp1 = node->handle_write_entries(req1);
    EXPECT_TRUE(resp1.success);

    WriteRequest req2;
    req2.id = 20;
    req2.input_vector = {2.0f};
    WriteResponse resp2 = node->handle_write_entries(req2);
    EXPECT_TRUE(resp2.success);

    WriteRequest req3;
    req3.id = 30;
    req3.input_vector = {3.0f};
    WriteResponse resp3 = node->handle_write_entries(req3);
    EXPECT_TRUE(resp3.success);

    // All 3 committed in order, with no residue from prior tests (captures cleared in SetUp).
    std::vector<CommitRecord> captures = mock_handler_raw->get_captures();
    ASSERT_EQ(captures.size(), 3u);
    EXPECT_EQ(captures[0].id, 10u);
    EXPECT_EQ(captures[1].id, 20u);
    EXPECT_EQ(captures[2].id, 30u);
}

// ============================================================================
// Majority Vote Tests
// Tests received_majority_votes (vote and AE overloads) indirectly through
// handle_write_entries. Each test creates its own leader; ~5-10s wait per test.
// ============================================================================

TEST(MajorityVoteTest, ThreeNodeClusterMajority) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    MockOutboundClient* client_raw = client.get();
    MockCommitHandler* handler_raw = handler.get();

    // Election: 1 yes vote suffices (self + 1 = 2/3 majority).
    client->vote_responses = {VoteResponse{0, true}, VoteResponse{0, false}};
    client->append_responses = {AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true}};

    std::unique_ptr<RaftNode> node = make_node(1, 2, std::move(client), std::move(handler));
    bool is_leader = wait_for_leader(*node);
    ASSERT_TRUE(is_leader);
    handler_raw->clear_captures();

    // Write: 1 of 2 peers ack → self + 1 = 2/3 ≥ majority(2) → success.
    client_raw->append_responses = {AppendEntriesResponse{0, true}, AppendEntriesResponse{0, false}};

    WriteRequest req;
    req.id = 1;
    req.input_vector = {1.0f};
    WriteResponse resp = node->handle_write_entries(req);

    EXPECT_TRUE(resp.success);
    EXPECT_TRUE(resp.is_leader);
}

TEST(MajorityVoteTest, ThreeNodeClusterNoMajority) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    MockOutboundClient* client_raw = client.get();
    MockCommitHandler* handler_raw = handler.get();

    client->vote_responses = {VoteResponse{0, true}, VoteResponse{0, true}};
    client->append_responses = {AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true}};

    std::unique_ptr<RaftNode> node = make_node(1, 2, std::move(client), std::move(handler));
    bool is_leader = wait_for_leader(*node);
    ASSERT_TRUE(is_leader);
    handler_raw->clear_captures();

    // Write: 0 peer acks → self only = 1/3 < majority(2) → fails.
    client_raw->append_responses = {};

    WriteRequest req;
    req.id = 2;
    req.input_vector = {2.0f};
    WriteResponse resp = node->handle_write_entries(req);

    EXPECT_FALSE(resp.success);
    EXPECT_TRUE(resp.is_leader);
}

TEST(MajorityVoteTest, FiveNodeClusterMajority) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    MockOutboundClient* client_raw = client.get();
    MockCommitHandler* handler_raw = handler.get();

    // Election: 2 yes votes (self + 2 = 3/5 majority).
    client->vote_responses = {
        VoteResponse{0, true}, VoteResponse{0, true},
        VoteResponse{0, false}, VoteResponse{0, false}
    };
    client->append_responses = {
        AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true},
        AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true}
    };

    std::unique_ptr<RaftNode> node = make_node(1, 4, std::move(client), std::move(handler));
    bool is_leader = wait_for_leader(*node);
    ASSERT_TRUE(is_leader);
    handler_raw->clear_captures();

    // Write: 2 of 4 peers ack → self + 2 = 3/5 ≥ majority(3) → success.
    client_raw->append_responses = {
        AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true},
        AppendEntriesResponse{0, false}, AppendEntriesResponse{0, false}
    };

    WriteRequest req;
    req.id = 3;
    req.input_vector = {3.0f};
    WriteResponse resp = node->handle_write_entries(req);

    EXPECT_TRUE(resp.success);
    EXPECT_TRUE(resp.is_leader);
}

TEST(MajorityVoteTest, FiveNodeClusterNoMajority) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    MockOutboundClient* client_raw = client.get();
    MockCommitHandler* handler_raw = handler.get();

    // Election: 3 yes votes (self + 3 = 4/5 majority — node wins cleanly).
    client->vote_responses = {
        VoteResponse{0, true}, VoteResponse{0, true},
        VoteResponse{0, true}, VoteResponse{0, false}
    };
    client->append_responses = {
        AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true},
        AppendEntriesResponse{0, true}, AppendEntriesResponse{0, true}
    };

    std::unique_ptr<RaftNode> node = make_node(1, 4, std::move(client), std::move(handler));
    bool is_leader = wait_for_leader(*node);
    ASSERT_TRUE(is_leader);
    handler_raw->clear_captures();

    // Write: 1 of 4 peers ack → self + 1 = 2/5 < majority(3) → fails.
    client_raw->append_responses = {
        AppendEntriesResponse{0, true}, AppendEntriesResponse{0, false},
        AppendEntriesResponse{0, false}, AppendEntriesResponse{0, false}
    };

    WriteRequest req;
    req.id = 4;
    req.input_vector = {4.0f};
    WriteResponse resp = node->handle_write_entries(req);

    EXPECT_FALSE(resp.success);
    EXPECT_TRUE(resp.is_leader);
}

TEST(MajorityVoteTest, SingleNodeAlwaysMajority) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    MockCommitHandler* handler_raw = handler.get();

    // peer_count=0: total=1, majority=1 — self-vote alone is sufficient.
    // vote_responses and append_responses stay empty; the math still passes.
    std::unique_ptr<RaftNode> node = make_node(1, 0, std::move(client), std::move(handler));
    bool is_leader = wait_for_leader(*node);
    ASSERT_TRUE(is_leader);
    handler_raw->clear_captures();

    WriteRequest req;
    req.id = 5;
    req.input_vector = {5.0f};
    WriteResponse resp = node->handle_write_entries(req);

    EXPECT_TRUE(resp.success);
    EXPECT_TRUE(resp.is_leader);
}

// ============================================================================
// Thread Safety Tests
// ============================================================================

TEST(ThreadSafetyTest, ConcurrentVoteRequests) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    const int num_threads = 4;
    std::vector<VoteResponse> responses(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&node, &responses, t]() {
            VoteRequest req = {1, static_cast<uint32_t>(t + 2), 0, 0};
            responses[t] = node.handle_vote_request(req);
        });
    }

    for (std::thread& th : threads) {
        th.join();
    }

    for (int t = 0; t < num_threads; ++t) {
        EXPECT_GE(responses[t].term, 1u) << "Thread " << t << " returned an unexpected term";
    }
}

TEST(ThreadSafetyTest, ConcurrentAppendEntries) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    RaftNode node(1, 2, std::move(client), std::move(handler));

    const int num_threads = 4;
    std::vector<AppendEntriesResponse> responses(num_threads);
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&node, &responses, t]() {
            AppendEntriesRequest req;
            req.leader_term = 1;
            req.leader_id = 99;
            req.prev_log_index = 0;
            req.prev_log_term = 0;
            req.leader_commit_index = 0;
            req.logs = {};
            responses[t] = node.handle_append_entries(req);
        });
    }

    for (std::thread& th : threads) {
        th.join();
    }

    for (int t = 0; t < num_threads; ++t) {
        EXPECT_GE(responses[t].term, 1u) << "Thread " << t << " returned an unexpected term";
    }
}

TEST(ThreadSafetyTest, ConcurrentWriteAndAppend) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    // peer_count=0: single node, self-elects; simplifies write success without
    // needing peer responses.
    RaftNode node(1, 0, std::move(client), std::move(handler));

    bool is_leader = wait_for_leader(node);
    ASSERT_TRUE(is_leader);

    std::atomic<bool> write_done(false);

    std::thread write_thread([&node, &write_done]() {
        WriteRequest req;
        req.id = 1;
        req.input_vector = {1.0f, 2.0f};
        node.handle_write_entries(req);
        write_done = true;
    });

    // These use a stale term (0) so the node rejects them without step-down,
    // keeping the leader alive for the concurrent write.
    std::thread append_thread1([&node]() {
        AppendEntriesRequest req;
        req.leader_term = 0;
        req.leader_id = 99;
        req.prev_log_index = 0;
        req.prev_log_term = 0;
        req.leader_commit_index = 0;
        req.logs = {};
        node.handle_append_entries(req);
    });

    std::thread append_thread2([&node]() {
        AppendEntriesRequest req;
        req.leader_term = 0;
        req.leader_id = 99;
        req.prev_log_index = 0;
        req.prev_log_term = 0;
        req.leader_commit_index = 0;
        req.logs = {};
        node.handle_append_entries(req);
    });

    write_thread.join();
    append_thread1.join();
    append_thread2.join();

    EXPECT_TRUE(write_done.load());
}

TEST(ThreadSafetyTest, NodeShutdownDuringOperations) {
    std::unique_ptr<MockOutboundClient> client = std::make_unique<MockOutboundClient>();
    std::unique_ptr<MockCommitHandler> handler = std::make_unique<MockCommitHandler>();
    std::unique_ptr<RaftNode> node = make_node(1, 2, std::move(client), std::move(handler));

    std::atomic<bool> keep_running(true);

    std::thread ops_thread([&node, &keep_running]() {
        while (keep_running.load()) {
            VoteRequest vote_req = {1, 2, 0, 0};
            node->handle_vote_request(vote_req);

            AppendEntriesRequest ae_req;
            ae_req.leader_term = 1;
            ae_req.leader_id = 99;
            ae_req.prev_log_index = 0;
            ae_req.prev_log_term = 0;
            ae_req.leader_commit_index = 0;
            ae_req.logs = {};
            node->handle_append_entries(ae_req);

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // Let ops run briefly, then signal stop and join before destroying the node.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    keep_running = false;
    ops_thread.join();

    // Destructor must not deadlock. It signals _shutdown and joins the election thread.
    node.reset();

    SUCCEED();
}

}  // namespace
}  // namespace tamdb
