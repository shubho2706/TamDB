
#include "tamdb/service/coord_node_service.h"

#include <iostream>

#include <grpcpp/grpcpp.h>
#include "tamdb.pb.h"
#include "tamdb.grpc.pb.h"


namespace tamdb {

CoordinatorNodeServiceImpl::CoordinatorNodeServiceImpl(CoordNodeConfig coord_node_config) 
                : _coord_node_config(coord_node_config) {


    for(NodeAddress& node_addr: _coord_node_config.data_nodes) {
        auto channel = grpc::CreateChannel(node_addr.ip + ":" + std::to_string(node_addr.port), 
                                        grpc::InsecureChannelCredentials());
        auto stub = tamdb::proto::DataNodeService::NewStub(channel);
        _data_node_stubs.push_back(std::move(stub));
    }
}

      
grpc::Status CoordinatorNodeServiceImpl::InsertVector(
                                                    grpc::ServerContext*, 
                                                    const proto::InsertRequest* insert_request, 
                                                    proto::InsertResponse* insert_response ) {
    
    
    for(auto& stub: _data_node_stubs) {
        grpc::ClientContext client_context;
        grpc::Status status = stub->InsertVector(&client_context, *insert_request, insert_response);
        if(status.ok()) {
            std::cout << "Response from stub " << insert_response->DebugString() << std::endl;
        } else {
            std::cout << "Response from stub " << status.error_message() << std::endl;
        }
        
    }

    return grpc::Status::OK;
}

grpc::Status CoordinatorNodeServiceImpl::SearchVector(
                                                    grpc::ServerContext*,
                                                    const proto::SearchRequest* search_request,
                                                    proto::SearchResponse* search_response) {     
        
    for(auto& stub: _data_node_stubs) {
        grpc::ClientContext client_context;

        tamdb::proto::SearchResponse node_search_res;

        grpc::Status status = stub->SearchVector(&client_context, *search_request, &node_search_res);
        if(status.ok()) {
            std::cout << "Response from stub " << search_response->DebugString() << std::endl;
            search_response->mutable_search_results()->MergeFrom(node_search_res.search_results());
        } else {
            std::cout << "Response from stub " << status.error_message() << std::endl;
        }
    }

    return grpc::Status::OK;

}
    
}
