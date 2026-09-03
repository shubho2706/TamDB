#pragma once

#include "tamdb.pb.h"
#include "tamdb.grpc.pb.h"

#include "tamdb/service/index_service.h"
#include "tamdb/service/search_service.h"


namespace tamdb {

/**
 * gRPC adapter for the tamDB service (the only class that touches proto types).
 *
 * Implements the generated TamDBService interface by translating each RPC:
 * unpacks the protobuf request into plain types, delegates to the
 * transport-agnostic IndexService / SearchService, then packs the result
 * back into the protobuf response. Register this (not the plain services)
 * with the gRPC ServerBuilder.
 */
class TamDBServiceImpl : public tamdb::proto::TamDBService::Service {
public:
    /**
     * @param index_service  Non-owning reference to the write facade; must
     *        outlive this adapter.
     * @param search_service Non-owning reference to the read facade; must
     *        outlive this adapter.
     */
    TamDBServiceImpl(IndexService& index_service, SearchService& search_service);

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
    /** Non-owning references to the logic facades (owned by GRPCServer). */
    IndexService& _index_service;
    SearchService& _search_service;
};
}