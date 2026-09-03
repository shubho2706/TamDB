#pragma once

#include "tamdb/hnsw/hnsw_index.h"

#include <cstdint>
#include <span>

namespace tamdb {

/**
 * Transport-agnostic write facade over the HNSW index.
 *
 * Wraps the shared index and exposes the insert operation as a plain,
 * protocol-independent call. The gRPC adapter (and any future HTTP layer)
 * delegates here; this class never references proto or gRPC types.
 */
class IndexService {

public:

    /**
     * @param hnswIndexPtr Shared handle to the index this service writes to.
     *        The same instance is shared with SearchService.
     */
    IndexService(HNSWIndexPtr hnswIndexPtr);

    /**
     * Insert a vector into the shared index.
     *
     * @param id User-provided unique identifier for the vector.
     * @param input_vector The vector data (must match the index dimensions).
     * @return true on success; false if the insert failed (e.g. dimension
     *         mismatch), swallowing the engine exception into a status flag.
     */
    bool insertVector(uint64_t id, const std::span<const float> input_vector);

private:

    /** Shared index instance (also used by SearchService). */
    HNSWIndexPtr _hnswIndexPtr;
};
}