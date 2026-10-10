#include <fstream>

#include <grpcpp/server_builder.h>

#include "tamdb/server/grpc_server.h"
#include "tamdb/wal/wal.h"
#include "tamdb/service/coord_node_service.h"
#include "tamdb/utils/json_utils.h"
#include "tamdb/adapter/raft_outbound_client.h"

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
    // TODO: Validate inputs

    CoordNodeConfig coord_node_config = config.coord_node_config.value_or(CoordNodeConfig());
    _coord_node_service_ptr = std::make_unique<CoordinatorNodeServiceImpl>(coord_node_config);
    builder.RegisterService(_coord_node_service_ptr.get());
}

void GRPCServer::build_data_node(grpc::ServerBuilder& builder, const ClusterConfig& config) {
    // TODO: Validate inputs
    VectorIndexConfig vector_index_config = config.vector_index_config.value_or(VectorIndexConfig ());
    HNSWIndexPtr hnswIndexPtr = std::make_shared<HNSWIndex>(
                        vector_index_config.max_connections_per_node_per_layer,
                        vector_index_config.ef_construction,
                        vector_index_config.dimensions);

    WriteAheadLoggerPtr walPtr = std::make_shared<WriteAheadLogger>("./tamdb_wal.bin");
    walPtr->replay(hnswIndexPtr);

    _index_service_ptr = std::make_unique<IndexService>(hnswIndexPtr, walPtr);
    _search_service_ptr = std::make_unique<SearchService>(hnswIndexPtr);

    std::vector<Node> peers;
    peers.reserve(config.coord_node_config.value().data_nodes.size());
    for(const Node& node : config.coord_node_config.value().data_nodes) {
        if(node.node_id != config.node_id)
            peers.push_back(node);
    }
    uint32_t peer_count = peers.size();
    RaftNodeConfig raft_node_conf = { 
        std::move(peers)
    };
    RaftOutboundClientPtr raft_client = std::make_unique<RaftOutboundClient>(raft_node_conf);    
    _raft_node_ptr = std::make_unique<RaftNode>(config.node_id, 
                                                peer_count,
                                                std::move(raft_client));

    _raft_service_ptr = std::make_unique<RaftServiceImpl>(*_raft_node_ptr);
    _data_node_service_ptr = std::make_unique<DataNodeServiceImpl>(*_index_service_ptr, 
                                                                *_search_service_ptr,
                                                                *_raft_node_ptr);
    builder.RegisterService(_data_node_service_ptr.get());
    builder.RegisterService(_raft_service_ptr.get());
}

void GRPCServer::shutdown() {
    if(_server){
        _server->Shutdown();
    }
        
};
}

