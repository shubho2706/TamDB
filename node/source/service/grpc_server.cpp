#include <fstream>

#include <grpcpp/server_builder.h>

#include "node/service/grpc_server.h"

#include "tamdb/wal/wal.h"
#include "node/service/coord_node_service.h"
#include "utils/json_utils.h"


namespace tamdb {

void GRPCServer::start(const std::string& address, const NodeRole& node_role, 
                        const std::string& node_config_file) {
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
        

    ClusterConfig config;

    std::ifstream config_file(node_config_file);
    if (!config_file.is_open()) {
        throw std::runtime_error("Unable to read config file: " + node_config_file);
    }
    nlohmann::json j = nlohmann::json::parse(config_file);
    config = j.get<ClusterConfig>();

    switch(node_role) {
        case NodeRole::DATA:
            build_data_node(builder, config);
            break;
        case NodeRole::COORDINATOR:
            build_coord_node(builder, config);
            break;
        case NodeRole::MASTER:
            // TODO: Master node for health checks and other cluster manage stuff
            break;
        case NodeRole::COMMON_NODE:
            build_data_node(builder, config);
            build_coord_node(builder, config);
            break;
    }

    _server = builder.BuildAndStart();
}



void GRPCServer::build_coord_node(grpc::ServerBuilder& builder, const ClusterConfig& config) {
    CoordNodeConfig coord_node_config = config.coord_node_config.value_or(CoordNodeConfig());
    _coord_node_service_ptr = std::make_unique<CoordinatorNodeServiceImpl>(coord_node_config);
    builder.RegisterService(_coord_node_service_ptr.get());
}

void GRPCServer::build_data_node(grpc::ServerBuilder& builder, const ClusterConfig& config) {
    DataNodeConfig data_node_config = config.data_node_config.value_or(DataNodeConfig());
    HNSWIndexPtr hnswIndexPtr = std::make_shared<HNSWIndex>(
                        data_node_config.max_connections_per_node_per_layer,
                        data_node_config.ef_construction,
                        data_node_config.dimensions);

    WriteAheadLoggerPtr walPtr = std::make_shared<WriteAheadLogger>("./tamdb_wal.bin");
    walPtr->replay(hnswIndexPtr);

    _index_service_ptr = std::make_unique<IndexService>(hnswIndexPtr, walPtr);
    _search_service_ptr = std::make_unique<SearchService>(hnswIndexPtr);

    _data_node_service_ptr = std::make_unique<DataNodeServiceImpl>(*_index_service_ptr, *_search_service_ptr);
    builder.RegisterService(_data_node_service_ptr.get());
}

void GRPCServer::shutdown() {
    if(_server){
        _server->Shutdown();
    }
        
};
}

