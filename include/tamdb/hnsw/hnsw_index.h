#pragma once

#include "tamdb/model/types.h"
#include <span>
#include <vector>
#include <cstdint>
#include <random>
#include <memory>
#include <shared_mutex>

namespace tamdb {

struct CandidateNode {
    uint32_t node_idx;
    float node_dist;
};

/**
 * A node in the HNSW graph.
 *
 * Each node represents a single vector in the index and maintains
 * adjacency lists (neighbor connections) for each layer it appears on.
 */
struct HNSWNode {
    /** User-provided unique identifier for this vector. */
    uint64_t _vector_id;

    /**
     * Adjacency lists for each layer this node exists on.
     * adj_list[layer] = list of node indices (into HNSWIndex::_nodes) that
     * are neighbors of this node on that layer.
     *
     * Layer 0 is the bottom (densest) layer where all nodes exist.
     * Higher layers contain exponentially fewer nodes with longer-range links.
     */
    std::vector<std::vector<uint32_t>> adj_list;
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
    void insert(uint64_t id, std::span<const float> vector);

    /**
     * Search for the nearest neighbors of a query vector.
     *
     * Traverses the graph from the top layer entry point down to layer 0,
     * performing a beam search on the bottom layer to find candidates.
     *
     * Time complexity: O(ef_search * log(N)) where ef_search >= top_k.
     *
     * @param query_vector The query vector. Must have exactly `dimensions` elements.
     * @param top_k Number of nearest neighbors to return.
     * @param ef_search Exploration factor for search. Controls how many candidates
     *          are explored during graph traversal. Higher values = better recall
     *          but slower search. Must be >= top_k. Default: 200.
     * @return Vector of SearchResult (id, distance) sorted by ascending distance.
     *         May return fewer than top_k results if the index has fewer vectors.
     */
    std::vector<SearchResult> search(std::span<const float> query_vector, size_t top_k, size_t ef_search = 200);

private:

    /**
     * Draw a random layer level for a newly inserted node.
     *
     * Uses the standard HNSW exponentially-decaying level distribution:
     * level = floor(-ln(U) / ln(M)), where U is uniform in (0, 1].
     * Level 0 is by far the most common; each higher level is exponentially
     * rarer, which is what keeps the upper layers sparse.
     *
     * @return The top layer this node will occupy (it exists on layers 0..level).
     */
    uint32_t random_layer();

    /**
     * Beam search over a single layer of the graph (the core HNSW primitive).
     *
     * Runs a greedy, best-first traversal on `curr_layer` starting from
     * `entry_points`, always expanding the closest unexplored node next and
     * keeping the `EF` nearest nodes seen so far. Terminates early once the
     * closest unexplored node is farther than the current worst keeper.
     *
     * Used by both insert (to find neighbor candidates and to zoom between
     * layers with EF=1) and search (with EF=ef_search on layer 0).
     *
     * @param entry_points Candidate nodes to seed the search from (only their
     *          node_idx is used; the distance is recomputed against input_vector).
     * @param EF Beam width: the number of nearest candidates to retain.
     * @param curr_layer The layer whose adjacency lists are traversed.
     * @param input_vector The query/target vector to measure distance against.
     * @return Up to `EF` candidates (internal index + distance), sorted by
     *         ascending distance (closest first).
     */
    std::vector<CandidateNode> search_layer(const std::vector<CandidateNode> entry_points,
                                    const uint32_t EF,
                                    const uint32_t curr_layer,
                                    std::span<const float> input_vector);


    /**
     * Wire a newly inserted node into one layer of the graph.
     *
     * Connects the new node to its nearest candidates on `layer` (up to M
     * edges, or 2*M on layer 0), adding each edge in both directions. When a
     * neighbor's degree exceeds its cap as a result, that neighbor is pruned
     * back down via prune_edge.
     *
     * @param insert_node_idx Internal index of the node being inserted.
     * @param layer The layer on which to add the edges.
     * @param possible_neighbours Candidate neighbors for this layer, sorted by
     *          ascending distance (as returned by search_layer). The closest
     *          M (or 2*M on layer 0) are chosen as neighbors.
     */
    void connect_layer(const uint32_t insert_node_idx, const uint32_t layer,
                        const std::vector<CandidateNode>& possible_neighbours);

    /**
     * Trim an over-connected node back to its edge cap on a layer.
     *
     * Called after a node exceeds its degree cap (M, or 2*M on layer 0).
     * Since a node overflows by exactly one edge per insert, this removes the
     * single neighbor farthest from `node_idx` (measured by L2 distance to
     * node_idx's own vector), deleting the edge in both directions.
     *
     * @param node_idx Internal index of the over-connected node to prune.
     * @param layer The layer whose adjacency list is trimmed.
     */
    void prune_edge(const uint32_t node_idx, uint32_t layer);

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
     */
    std::vector<float> _flat_vectors;

    /**
     * All nodes in the index, stored in insertion order.
     * Node index (position in this vector) is used as the internal ID
     * for adjacency lists and graph traversal.
     */
    std::vector<HNSWNode> _nodes;

    static constexpr uint32_t EMPTY_ROOT = UINT32_MAX;

    /**
     * Index (into _nodes) of the entry point node.
     * This is the node with the highest layer level, used as the
     * starting point for all searches and insertions.
     */
    uint32_t _root = EMPTY_ROOT;

    /**
     * For random Level Generation
     */
    std::mt19937 _rng;
    std::uniform_real_distribution<double> _level_dist;

    /**
     * Control multi threaded insert and search
     */
    std::shared_mutex _mutex;
};

typedef std::shared_ptr<HNSWIndex> HNSWIndexPtr;

} // namespace tamdb
