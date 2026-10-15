
#include "tamdb/service/coord_node_service.h"

#include <iostream>

#include <grpcpp/grpcpp.h>
#include "tamdb.pb.h"
#include "tamdb.grpc.pb.h"


namespace tamdb {

CoordinatorNodeServiceImpl::CoordinatorNodeServiceImpl(CoordNodeConfig coord_node_config) 
                : _coord_node_config(coord_node_config) {
    // TODO: Form a map of the  node id to stub
    for(Node& node: _coord_node_config.data_nodes) {
        auto channel = grpc::CreateChannel(node.address.ip + ":" + std::to_string(node.address.port), 
                                        grpc::InsecureChannelCredentials());
        auto stub = tamdb::proto::DataNodeService::NewStub(channel);
        _node_to_stub_map[node.node_id] = std::move(stub);
        
    }
}
      
grpc::Status CoordinatorNodeServiceImpl::InsertVector(
                                                    grpc::ServerContext*, 
                                                    const proto::InsertRequest* insert_request, 
                                                    proto::InsertResponse* insert_response ) {
    for(auto& [key, stub]: _node_to_stub_map) {
        grpc::Status status = insert_vector_helper(insert_request, insert_response, *stub);
        if(!status.ok()) {
            uint32_t leader_node_id = insert_response->leader_id();
            // Data Node has responed back with the correct Leader, lets sent the request there
            auto iter = _node_to_stub_map.find(leader_node_id);
            if(iter != _node_to_stub_map.end()) {
                status = insert_vector_helper(insert_request, insert_response, *(iter->second));
                if(status.ok()) {
                    break;
                }
            }
        } else {
            break; // The first stub itself was the leader
        }
    }

    return grpc::Status::OK;
}

grpc::Status CoordinatorNodeServiceImpl::insert_vector_helper(
                                const proto::InsertRequest* insert_req, 
                                proto::InsertResponse* insert_res, 
                                tamdb::proto::DataNodeService::Stub& stub) {
    grpc::ClientContext client_context;
    grpc::Status status = stub.InsertVector(&client_context, *insert_req, insert_res);
    if(status.ok()) {
            std::cout << " COORD Response from stub " << insert_res->DebugString() << std::endl;
    } else {
        std::cout << " COORD Response from stub " << status.error_message() << std::endl;
    }
    
    return status;
}

grpc::Status CoordinatorNodeServiceImpl::SearchVector(
                                                    grpc::ServerContext*,
                                                    const proto::SearchRequest* search_request,
                                                    proto::SearchResponse* search_response) {     
    for(auto& [key, stub]: _node_to_stub_map) {
        grpc::ClientContext client_context;

        tamdb::proto::SearchResponse node_search_res;

        grpc::Status status = stub->SearchVector(&client_context, *search_request, &node_search_res);
        if(status.ok()) {
            std::cout << " COORD Response from stub " << search_response->DebugString() << std::endl;
            // TODO: Perform Top K instead of merging blindly after we implement sharding and replication
            search_response->mutable_search_results()->MergeFrom(node_search_res.search_results());
            break;
        } else {
            std::cout << " COORD Response from stub " << status.error_message() << std::endl;
        }
    }

    return grpc::Status::OK;

}
    
}
