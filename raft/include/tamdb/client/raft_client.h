#pragma once

#include <vector>
#include <memory>

#include "tamdb/models/raft_models.h"
namespace tamdb {

/**
 * Abstract interface for Raft peer communication.
 * RaftNode calls these methods to send messages to peers.
 * Implementations handle the transport (gRPC, TCP, etc).
 */
class IRaftOutboundClient {

public:
    /**
     * Send vote requests to all peers and collect responses.
     *
     * @param vote_request The vote request containing candidate term, ID, and log info.
     * @return Vector of VoteResponse from each reachable peer.
     */
    virtual std::vector<VoteResponse> send_request_votes(const VoteRequest& vote_request) = 0;

    /**
     * Send AppendEntries to all peers and collect responses.
     *
     * @param append_entries_req The request containing leader term, log entries, and commit index.
     * @return Vector of AppendEntriesResponse from each reachable peer.
     */
    virtual std::vector<AppendEntriesResponse> send_append_entries(const AppendEntriesRequest& append_entries_req) = 0;

    /** Virtual destructor for proper cleanup through base pointer. */
    virtual ~IRaftOutboundClient() = default;

};

typedef std::unique_ptr<IRaftOutboundClient> RaftOutboundClientPtr;
}
