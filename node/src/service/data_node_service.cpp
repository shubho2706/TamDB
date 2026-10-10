#include "tamdb/service/data_node_service.h"

namespace tamdb {

DataNodeServiceImpl::DataNodeServiceImpl(IndexService& index_service, SearchService& search_service, 
                                        RaftNode& raft_node) 
                : _index_service(index_service), 
                _search_service(search_service),
                _raft_node(raft_node)
{}

grpc::Status DataNodeServiceImpl::InsertVector(grpc::ServerContext*,
                                            const proto::InsertRequest* insert_request,
                                            proto::InsertResponse* insert_response) {
    
    // std::span<const float> insert_vec (insert_request->input_vector().data(), 
    //                                     insert_request->input_vector().size());                                         
    // bool res = _index_service.insertVector(insert_request->id(), insert_vec);
    // insert_response->set_ok(res);
    
    std::vector<float> input_vector {insert_request->input_vector().begin(), 
                                    insert_request->input_vector().end()};

    WriteRequest write_request = {
        insert_request->id(),
        std::move(input_vector)
    };

    WriteResponse resp = _raft_node.handle_write_entries(write_request);

    if(resp.success) {
        insert_response->set_ok(true);
        return grpc::Status::OK;
    } 

    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "Not a leader. Leader is " + std::to_string(resp.leader_id));

}

grpc::Status DataNodeServiceImpl::SearchVector(grpc::ServerContext*,
                                            const proto::SearchRequest* search_request,
                                            proto::SearchResponse* search_response) {

    std::span<const float> search_vec (search_request->search_vector().data(), 
                                        search_request->search_vector().size());
    std::vector<SearchResult> result_vectors = _search_service.searchVector(search_vec, 
                                                        search_request->top_k(), 
                                                        search_request->ef_search());

    for(auto& sr: result_vectors) {
        proto::SearchResult* out = search_response->add_search_results();
        out->set_id(sr.id);
        out->set_score(sr.score);
    }

    return grpc::Status::OK;
}
}