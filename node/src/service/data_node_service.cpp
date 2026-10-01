#include "tamdb/service/data_node_service.h"

namespace tamdb {

DataNodeServiceImpl::DataNodeServiceImpl(IndexService& index_service, SearchService& search_service) 
                : _index_service(index_service), _search_service(search_service)
{}

grpc::Status DataNodeServiceImpl::InsertVector(grpc::ServerContext*,
                                            const proto::InsertRequest* insert_request,
                                            proto::InsertResponse* insert_response) {
    
    std::span<const float> insert_vec (insert_request->input_vector().data(), 
                                        insert_request->input_vector().size());                                         
    bool res = _index_service.insertVector(insert_request->id(), insert_vec);
    insert_response->set_ok(res);
    return grpc::Status::OK;

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