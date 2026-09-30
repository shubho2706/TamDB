#pragma once

#include "tamdb/model/types.h"
#include "tamdb/hnsw/hnsw_index.h"

#include <vector>
#include <span>

namespace tamdb {

/**
 * Transport-agnostic read facade over the HNSW index.
 *
 * Wraps the shared index and exposes nearest-neighbor search as a plain,
 * protocol-independent call. The gRPC adapter delegates here; this class
 * never references proto or gRPC types.
 */
class SearchService {

public:

    /**
     * @param hnswIndexPtr Shared handle to the index this service reads from.
     *        The same instance is shared with IndexService.
     */
    SearchService(HNSWIndexPtr hnswIndexPtr);

    /**
     * Search the shared index for the nearest neighbors of a query.
     *
     * @param search_vector The query vector (must match the index dimensions).
     * @param top_k Number of nearest neighbors to return.
     * @param ef_search Beam width for the search (>= top_k; higher = better
     *        recall, slower).
     * @return Up to top_k results sorted by ascending distance.
     */
    std::vector<SearchResult> searchVector(std::span<const float> search_vector, size_t top_k, size_t ef_search);

private:
    /** Shared index instance (also used by IndexService). */
    HNSWIndexPtr _hnswIndexPtr;
};
}