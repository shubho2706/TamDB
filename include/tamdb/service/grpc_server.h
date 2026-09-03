#pragma once

#include "tamdb/service/grpc_adapter.h"

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
    void start(const std::string& address);

    /**
     * Stop the server: refuse new RPCs, drain in-flight ones, and unblock
     * any thread waiting on the server. Safe to call if never started.
     */
    void shutdown();

private:
    
    std::unique_ptr<IndexService> _indexServicePtr;
    std::unique_ptr<SearchService> _searchServicePtr;
    std::unique_ptr<TamDBServiceImpl> _tamDBServiceAdapter;

    // This needs to be declared last to ensure this is the first object to be destroyed
    std::unique_ptr<grpc::Server> _server;
};

}