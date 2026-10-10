#include "tamdb/service/raft_service.h"

namespace tamdb {
RaftServiceImpl::RaftServiceImpl(RaftNode& raft_node)
            : _raft_node (raft_node) {}

grpc::Status RaftServiceImpl::RequestVote(grpc::ServerContext*, 
                                        const tamdb::proto::raft::VoteRequest* vote_request,
                                        tamdb::proto::raft::VoteResponse* vote_response) {
    tamdb::VoteRequest vote_req = {
        vote_request->term(),
        vote_request->candidate_id(),
        vote_request->last_log_index(),
        vote_request->last_log_term()
    };
    tamdb::VoteResponse vote_resp = _raft_node.handle_vote_request(vote_req);

    vote_response->set_term(vote_resp.term);
    vote_response->set_vote(vote_resp.vote);

    return grpc::Status::OK;
}

grpc::Status RaftServiceImpl::AppendEntries(grpc::ServerContext*,
                                            const tamdb::proto::raft::AppendEntriesRequest* append_entries_request,
                                            tamdb::proto::raft::AppendEntriesResponse* append_entries_response) {
    tamdb::AppendEntriesRequest req = {
        append_entries_request->leader_term(),
        append_entries_request->leader_id(),
        append_entries_request->prev_log_index(),
        append_entries_request->prev_log_term(),
        append_entries_request->leader_commit_index(),
    };
 
    std::vector<tamdb::LogEntry> logs;
    for(const tamdb::proto::raft::LogEntry& log: append_entries_request->logs()) {
        const tamdb::proto::raft::Datum& datum = log.datum();
        logs.push_back({
                log.term(), 
                log.log_index(),
                {   
                    datum.vector_id(),
                    {datum.input_vector().begin(), datum.input_vector().end()}
                }
        });
    }

    tamdb::AppendEntriesResponse resp = _raft_node.handle_append_entries(req);
    
    append_entries_response->set_term(resp.term);
    append_entries_response->set_success(resp.success);

    return grpc::Status::OK;

}
    
}