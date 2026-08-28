#include "hnsw/hnsw_index.h"
#include "utils/distance.h"

#include <random>
#include <limits>
#include <set>
#include <algorithm>
#include <vector>
#include <queue>
#include <span>


namespace tamdb {



HNSWIndex::HNSWIndex(uint32_t M, uint32_t ef_construction, size_t dimensions) 
    : _M(M), 
    _ef_construction(ef_construction), 
    _dimensions(dimensions),
    _rng(std::random_device{}()),
    _level_dist(0.0, 1.0)
{}

void HNSWIndex::insert(uint64_t id, std::span<float> vector) {
    if(vector.size() != _dimensions)
        throw std::invalid_argument("dimension mismatch");
    


    // find the offset and insert the new vector
    uint32_t offset = _flat_vectors.size();
    for(size_t i = 0; i < vector.size(); ++i) {
        _flat_vectors.push_back(vector[i]);
    }

    uint32_t level = random_layer();
    std::vector<std::vector<uint32_t>> adj_list;
        for(int i = 0; i <= level; ++i) {
        adj_list.push_back(std::vector<uint32_t>{});
    }

    _nodes.push_back({id, std::move(adj_list), offset});
    // Build the node into the graph


    uint32_t u_idx = _root;
    uint32_t layer = _nodes[u_idx].adj_list.size() - 1;
    while(layer > 0) {
        uint32_t layer_min_idx = search_layer(u_idx, layer, vector);
        --layer;
        u_idx = layer_min_idx;
    }


    // init the root  TODO: come back to this placement
    if(_root == -1)
        _root = 0;
}


std::vector<CandidateNode> HNSWIndex::search_layer(const std::vector<uint32_t> entry_points, const uint32_t EF, 
                                    const uint32_t curr_layer, std::span<const float> input_vector) {
    
    // PQ for capturing pop #ef nodes
    auto max_heap_cmp = [] (const CandidateNode &a, const CandidateNode& b) {
        return a.node_dist < b.node_dist; 
    };
    std::priority_queue<CandidateNode,
                        std::vector<CandidateNode>,
                        decltype(max_heap_cmp)> max_heap(max_heap_cmp);

    // PQ for BFS traversal
    auto min_heap_cmp = [] (const CandidateNode& a, const CandidateNode& b) {
        return b.node_dist < a.node_dist; 
    };
    std::priority_queue<CandidateNode,
                        std::vector<CandidateNode>,
                        decltype(min_heap_cmp)> min_heap(min_heap_cmp);

    std::set<uint32_t> visited_nodes;
    for(uint32_t u_idx: entry_points) {
        float l2_dist = l2_distance(input_vector, 
                                    std::span<const float>{_flat_vectors.data() + u_idx * _dimensions, _dimensions});
        min_heap.push({u_idx, l2_dist});
        visited_nodes.insert(u_idx);
        max_heap.push({u_idx, l2_dist});
        if(max_heap.size() > EF)
            max_heap.pop();
    }
    

    while(! min_heap.empty()) {
        CandidateNode u_cand = min_heap.top();
        min_heap.pop();

        // if the best possible node's dist is worse than the worst possible node in the result list AND results is full
        if(u_cand.node_dist > max_heap.top().node_dist && max_heap.size() == EF)
            break;

        for(uint32_t v_idx: _nodes[u_cand.node_idx].adj_list[curr_layer]) {
            if(visited_nodes.find(v_idx) == visited_nodes.end()) {

                float l2_dist = l2_distance(input_vector, 
                                            std::span<const float>{_flat_vectors.data() + v_idx * _dimensions, _dimensions});
                
                // if the results is not full or worst item in result is worse than selected node
                if(max_heap.size() < EF || (l2_dist < max_heap.top().node_dist && max_heap.size() == EF)) {
                    min_heap.push({v_idx, l2_dist});
                    visited_nodes.insert(v_idx);
                    max_heap.push({v_idx, l2_dist});
                    if(max_heap.size() > EF)
                        max_heap.pop();
                    
                }
            }
        }
    }

    std::vector<CandidateNode> top_ef_nodes;
    while(! max_heap.empty()) {
        top_ef_nodes.push_back(max_heap.top());
        max_heap.pop();
    }

    std::reverse(top_ef_nodes.begin(), top_ef_nodes.end());

    return top_ef_nodes; 

}

std::vector<SearchResult> HNSWIndex::search(std::span<const float> query, size_t pop_k, size_t ef_search) {

}


uint32_t HNSWIndex::random_layer() {
    return std::floor( (-1) * std::log(_level_dist(_rng)) * (1 / std::log(_M)));
}

};