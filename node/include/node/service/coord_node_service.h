#pragma once

#include "tamdb.pb.h"
#include "tamdb.grpc.pb.h"

#include <vector>
#include "node/constants/node_config.h"

namespace tamdb {
/**
 * gRPC service implementation for a coordinator node.
 *
 * Forwards InsertVector and SearchVector RPCs across all registered data-node stubs,
 * aggregating results before returning them to the caller.
 */
class CoordinatorNodeServiceImpl: public tamdb::proto::CoordNodeService::Service {

public:

    /**
     * Constructs the coordinator service and creates gRPC channel stubs for each
     * data node listed in coord_node_config.
     */
    CoordinatorNodeServiceImpl(CoordNodeConfig coord_node_config);
                
    /**
     * RPC handler: insert a vector.
     * Unpacks id + vector from the request, calls IndexService::insertVector,
     * and reports success via InsertResponse.ok.
     */
    grpc::Status InsertVector(grpc::ServerContext*,
                            const proto::InsertRequest* insert_req,
                            proto::InsertResponse* insert_res) override;

    /**
     * RPC handler: nearest-neighbor search.
     * Unpacks the query + top_k + ef_search, calls SearchService::searchVector,
     * and appends each result into SearchResponse.search_results.
     */
    grpc::Status SearchVector(grpc::ServerContext*,
                            const proto::SearchRequest* search_req,
                            proto::SearchResponse* search_res) override;


private: 
    CoordNodeConfig _coord_node_config;
    std::vector<std::unique_ptr<tamdb::proto::DataNodeService::Stub>> _data_node_stubs;
};

typedef std::unique_ptr<CoordinatorNodeServiceImpl> CoordNodeServicePtr;
}
