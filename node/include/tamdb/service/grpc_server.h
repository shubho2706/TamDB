#pragma once

#include "tamdb/service/data_node_service.h"
#include "tamdb/service/coord_node_service.h"
#include "tamdb/constants/node_config.h"
#include "tamdb/constants/node_roles.h"

#include <grpcpp/server.h>
#include <string>
#include <memory>

namespace tamdb {

/**
 * Owns and runs the gRPC server, and wires together the service layer.
 *
 * Composition root for the serving stack: creates the shared HNSW index,
 * the read/write facades, and the gRPC adapter, registers the adapter, and
 * manages the underlying grpc::Server lifecycle.
 */
class GRPCServer {

public:
    /**
     * Build and start the server (non-blocking).
     *
     * Creates the index + services + adapter, registers the adapter, and
     * begins serving on gRPC's own threads. Returns immediately; the server
     * keeps running until shutdown().
     *
     * @param address Listen address, e.g. "0.0.0.0:50051".
     */
    void start(const std::string& address, const NodeRole& node_role, const std::string& node_config_file);

    /**
     * Stop the server: refuse new RPCs, drain in-flight ones, and unblock
     * any thread waiting on the server. Safe to call if never started.
     */
    void shutdown();

private:
    /**
     * Initialises IndexService, SearchService, and DataNodeServiceImpl,
     * then registers the data-node adapter with the provided ServerBuilder.
     */
    void build_data_node(grpc::ServerBuilder& builder, const ClusterConfig& config);
    
    /**
     * Initialises CoordinatorNodeServiceImpl and registers it with the provided ServerBuilder.
     */
    void build_coord_node(grpc::ServerBuilder& builder, const ClusterConfig& config);

    std::unique_ptr<IndexService> _index_service_ptr;
    std::unique_ptr<SearchService> _search_service_ptr;
    DataNodeServicePtr _data_node_service_ptr;

    CoordNodeServicePtr _coord_node_service_ptr;

    // This needs to be declared last to ensure this is the first object to be destroyed
    std::unique_ptr<grpc::Server> _server;
};

}
