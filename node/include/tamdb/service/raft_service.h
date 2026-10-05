#pragma once

#include "raft.pb.h"
#include "raft.grpc.pb.h"



namespace tamdb {

/**
 * Inbound gRPC adapter for Raft RPCs.
 * Receives RequestVote and AppendEntries calls from peers,
 * converts proto types to Raft structs, and delegates to RaftNode.
 */
class RaftServiceImpl : public tamdb::proto::raft::RaftService::Service {

public: 

    /**
     * Constructs the inbound service with a reference to the local RaftNode.
     *
     * @param raft_node_ptr Shared pointer to the RaftNode that handles consensus logic.
     */
    RaftServiceImpl(RaftNodePtr raft_node_ptr);

    /**
     * Handle incoming vote request from a candidate peer.
     * Converts proto to struct, delegates to RaftNode, converts response back.
     *
     * @param vote_request  Incoming proto vote request from a candidate.
     * @param vote_response Outgoing proto vote response to the candidate.
     * @return grpc::Status::OK on success.
     */
    grpc::Status RequestVote(grpc::ServerContext*, 
                            const proto::VoteRequest* vote_request,
                            proto::VoteResponse* vote_response) override;

    /**
     * Handle incoming AppendEntries from leader.
     * Serves as heartbeat and log replication.
     * Converts proto to struct, delegates to RaftNode, converts response back.
     *
     * @param append_entries_request  Incoming proto AppendEntries from leader.
     * @param append_entries_response Outgoing proto response to leader.
     * @return grpc::Status::OK on success.
     */
    grpc::Status AppendEntries(grpc::ServerContext*,
                            const proto::AppendEntriesRequest* append_entries_request,
                            proto::AppendEntriesResponse* append_entries_response) override;

private:
    RaftNodePtr _raft_node_ptr;
    std::vector<std::unique_ptr<tamdb::proto::RaftService::Stub>> _peer_nodes;
};
}

