#pragma once

#include <vector>

#include "tamdb/client/raft_client.h"
#include "tamdb/models/raft_models.h"
#include "tamdb/service/raft_service.h"
#include "tamdb/constants/node_config.h"

namespace tamdb {
/**
 * gRPC implementation of RaftOutboundClient.
 * Holds stubs to all peer nodes in the shard group and forwards
 * vote requests and AppendEntries RPCs over the network.
 */
class RaftOutboundClient : public IRaftOutboundClient {
public: 

    /**
     * Creates gRPC channel stubs for each peer node in the config.
     *
     * @param raft_node_config Peer node addresses for this shard group.
     */
    RaftOutboundClient(const RaftNodeConfig& raft_node_config);

    /**
     * Send vote requests to all peers. Returns collected responses.
     *
     * @param vote_request The vote request to broadcast to all peers.
     * @return Vector of VoteResponse from each reachable peer.
     */
    std::vector<VoteResponse> send_request_votes(const VoteRequest& vote_request);

    /**
     * Send AppendEntries RPCs to all peers. Returns collected responses.
     *
     * @param append_entries_req The request containing leader term, log entries, and commit index.
     * @return Vector of AppendEntriesResponse from each reachable peer.
     */
    std::vector<AppendEntriesResponse> send_append_entries(const AppendEntriesRequest& append_entries_req);

    /**
     * 
     */
    ~RaftOutboundClient() = default;
private:
    std::vector<std::unique_ptr<tamdb::proto::raft::RaftService::Stub>> _peer_node_stubs;

};
}
