#pragma once

#include "model/types.h"
#include <span>
#include <vector>
#include <cstdint>

namespace tamdb {

/**
 * A node in the HNSW graph.
 *
 * Each node represents a single vector in the index and maintains
 * adjacency lists (neighbor connections) for each layer it appears on.
 */
struct HNSWNode {
    /** User-provided unique identifier for this vector. */
    uint64_t _id;

    /**
     * Adjacency lists for each layer this node exists on.
     * adj_list[layer] = list of node indices (into HNSWIndex::_nodes) that
     * are neighbors of this node on that layer.
     *
     * Layer 0 is the bottom (densest) layer where all nodes exist.
     * Higher layers contain exponentially fewer nodes with longer-range links.
     */
    std::vector<std::vector<uint32_t>> adj_list;

    /**
     * Offset into HNSWIndex::_flat_vector where this node's vector data begins.
     * The vector occupies _flat_vector[vector_offset .. vector_offset + dimensions).
     */
    uint32_t vector_offset;
};


/**
 * HNSW (Hierarchical Navigable Small World) index for approximate nearest neighbor search.
 *
 * Builds a multi-layer navigable small world graph where:
 * - Layer 0 contains ALL vectors with short-range connections
 * - Higher layers contain exponentially fewer vectors with long-range connections
 * - Search starts at the top layer and greedily descends to layer 0
 *
 * This provides O(log N) search time vs O(N) for flat/brute-force search,
 * at the cost of approximate (not exact) results.
 *
 * Key parameters:
 * - M: max connections per node per layer (controls graph connectivity)
 * - ef_construction: exploration factor during insert (controls build quality)
 *
 * Reference: "Efficient and robust approximate nearest neighbor using Hierarchical
 * Navigable Small World graphs" - Malkov & Yashunin, 2018
 */
class HNSWIndex {

public:

    /**
     * Construct a new HNSW index.
     *
     * @param M Maximum number of connections per node per layer.
     *          Higher M = better recall but more memory and slower inserts.
     *          Layer 0 uses 2*M connections. Typical value: 16.
     * @param ef_construction Exploration factor during index building.
     *          Controls how many candidates are explored when finding neighbors
     *          for a newly inserted node. Must be >= M. Typical value: 200.
     * @param dimensions Number of dimensions for each vector (e.g., 768 for BERT embeddings).
     */
    HNSWIndex(uint32_t M, uint32_t ef_construction, size_t dimensions);

    /**
     * Insert a new vector into the index.
     *
     * Assigns the vector a random layer level, then connects it to its
     * nearest neighbors on each layer from the assigned level down to layer 0.
     *
     * Time complexity: O(ef_construction * log(N)) amortized.
     *
     * @param id User-provided unique identifier for this vector.
     * @param vector The vector data. Must have exactly `dimensions` elements.
     * @throws std::invalid_argument if vector.size() != dimensions.
     */
    void insert(uint64_t id, std::span<float> vector);

    /**
     * Search for the nearest neighbors of a query vector.
     *
     * Traverses the graph from the top layer entry point down to layer 0,
     * performing a beam search on the bottom layer to find candidates.
     *
     * Time complexity: O(ef_search * log(N)) where ef_search >= top_k.
     *
     * @param query The query vector. Must have exactly `dimensions` elements.
     * @param top_k Number of nearest neighbors to return.
     * @param ef_search Exploration factor for search. Controls how many candidates
     *          are explored during graph traversal. Higher values = better recall
     *          but slower search. Must be >= top_k. Default: 200.
     * @return Vector of SearchResult (id, distance) sorted by ascending distance.
     *         May return fewer than top_k results if the index has fewer vectors.
     */
    std::vector<SearchResult> search(std::span<const float> query, size_t top_k, size_t ef_search = 200);

private:

    /** Number of dimensions per vector (e.g., 768). */
    size_t _dimensions;

    /**
     * Exploration factor for construction.
     * Controls beam width when searching for neighbors during insert.
     * Higher values produce a better quality graph at the cost of slower inserts.
     */
    uint32_t _ef_construction;

    /**
     * Maximum number of connections per node per layer.
     * Layer 0 allows up to 2*M connections (Mmax0) for better recall
     * on the densest layer. All other layers allow up to M connections.
     */
    uint32_t _M;

    /**
     * Contiguous storage for all vector data.
     * Vectors are stored end-to-end: [vec0_dim0, vec0_dim1, ..., vec1_dim0, ...].
     * Node i's vector starts at offset _nodes[i].vector_offset.
     */
    std::vector<float> _flat_vector;

    /**
     * All nodes in the index, stored in insertion order.
     * Node index (position in this vector) is used as the internal ID
     * for adjacency lists and graph traversal.
     */
    std::vector<HNSWNode> _nodes;

    /**
     * Index (into _nodes) of the entry point node.
     * This is the node with the highest layer level, used as the
     * starting point for all searches and insertions.
     */
    uint32_t _root;
};

} // namespace tamdb
