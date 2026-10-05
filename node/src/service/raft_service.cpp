#include "tamdb/service/raft_service.h"

namespace tamdb {
RaftServiceImpl::RaftServiceImpl(RaftNodePtr& raft_node_ptr)
            : _raft_node_ptr (raft_node_ptr) {}

grpc::Status RaftServiceImpl::RequestVote(grpc::ServerContext*, 
                                        const proto::VoteRequest* vote_request,
                                        proto::VoteResponse* vote_response) {
    tamdb::VoteRequest vote_req = {
        vote_request->term(),
        vote_request->candidate_id(),
        vote_request->last_log_index(),
        vote_request->last_log_term()
    };
    tamdb::VoteResponse vote_resp = _raft_node_ptr->handle_vote_request(vote_req);

    vote_response->set_term(vote_resp.term);
    vote_response->set_vote(vote_resp.vote);

    return grpc::Status::OK;
}

grpc::Status RaftServiceImpl::AppendEntries(grpc::ServerContext*,
                                            const proto::AppendEntriesRequest* append_entries_request,
                                            proto::AppendEntriesResponse* append_entries_response) {
    tamdb::AppendEntriesRequest req = {
        append_entries_request->leader_term(),
        append_entries_request->leader_id(),
        append_entries_request->prev_log_index(),
        append_entries_request->prev_log_term(),
        append_entries_request->leader_commit_index(),
    };

    std::vector<tamdb::LogsEntry> logs;
    for(const auto& log: append_entries_request->logs) {
        logs.push_back({log.term(), log.log_index(),
                        {log.input_vector.begin(), log.input_vector.end()}});
    }

    tamdb::AppendEntriesResponse resp = _raft_node_ptr->handle_append_entries(req);
    
    append_entries_response->set_term(resp.term);
    append_entries_response->set_success(resp.success);

    return grpc::Status::OK;

}
    
}