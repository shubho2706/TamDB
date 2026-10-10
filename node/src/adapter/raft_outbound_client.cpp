#include "tamdb/adapter/raft_outbound_client.h"

#include <grpcpp/grpcpp.h>
#include "raft.pb.h"
#include "raft.grpc.pb.h"

namespace tamdb {

RaftOutboundClient::RaftOutboundClient(const RaftNodeConfig& raft_node_config) {
    for(const Node& node: raft_node_config.peer_nodes) {
        // TODO:  possible to create persistant connections ?
        auto channel = grpc::CreateChannel(node.address.ip + ":"+ std::to_string(node.address.port),
                                            grpc::InsecureChannelCredentials());
        auto stub = tamdb::proto::raft::RaftService::NewStub(channel);
        _peer_node_stubs.push_back(std::move(stub));
    }                        
}

std::vector<VoteResponse> RaftOutboundClient::send_request_votes(const VoteRequest& vote_request) {

    tamdb::proto::raft::VoteRequest vote_request_proto;

    vote_request_proto.set_term(vote_request.term);
    vote_request_proto.set_candidate_id(vote_request.candidate_id);
    vote_request_proto.set_last_log_index(vote_request.last_log_index);
    vote_request_proto.set_last_log_term(vote_request.last_log_term);

    std::vector<VoteResponse> vote_responses;

    for(auto& stub: _peer_node_stubs) {
        tamdb::proto::raft::VoteResponse vote_response_proto;

        grpc::ClientContext client_context;
        grpc::Status status = stub->RequestVote(&client_context, vote_request_proto, &vote_response_proto);

        if(status.ok()) {
            std::cout << "RaftOutboundClient Response from stub " << vote_response_proto.DebugString() << std::endl;
            // TODO: send back the responses
            vote_responses.push_back({
                    vote_response_proto.term(),
                    vote_response_proto.vote()
                });
        } else {
            std::cout << "Error response from stub " << status.error_message() << std::endl;
        }
    }

    return vote_responses;
}

std::vector<AppendEntriesResponse> RaftOutboundClient::send_append_entries(const AppendEntriesRequest& append_entries_req) {
    tamdb::proto::raft::AppendEntriesRequest append_entries_req_proto;

    append_entries_req_proto.set_leader_term(append_entries_req.leader_term);
    append_entries_req_proto.set_leader_id(append_entries_req.leader_id);
    append_entries_req_proto.set_prev_log_index(append_entries_req.prev_log_index);
    append_entries_req_proto.set_prev_log_term(append_entries_req.prev_log_term);
    append_entries_req_proto.set_leader_commit_index(append_entries_req.leader_commit_index);

    for(const auto& entry: append_entries_req.logs) {
        auto* proto_entry = append_entries_req_proto.add_logs();

        proto_entry->set_term(entry.term);
        proto_entry->set_log_index(entry.log_index);

        auto* proto_datum = proto_entry->mutable_datum();
        proto_datum->set_vector_id(entry.datum.vector_id);
        for(float val : entry.datum.input_vector) {
            proto_datum->add_input_vector(val);
        }
    }

    std::vector<AppendEntriesResponse> append_entries_responses;

    for(auto& stub: _peer_node_stubs) {
        tamdb::proto::raft::AppendEntriesResponse append_entries_response_proto;

        grpc::ClientContext client_context;
        grpc::Status status = stub->AppendEntries(&client_context, append_entries_req_proto, &append_entries_response_proto);

        if(status.ok()) {
            std::cout << "RaftOutboundClient Response from stub" << append_entries_response_proto.DebugString();
            // TODO: send back the responses
            append_entries_responses.push_back({
                append_entries_response_proto.term(),
                append_entries_response_proto.success()
            });
        } else {
            std::cout << "RaftOutboundClient Error Response from stub " << status.error_message() << std::endl;
        }
    }

    return append_entries_responses;
}
}
