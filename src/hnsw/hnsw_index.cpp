#include "tamdb/hnsw/hnsw_index.h"
#include "tamdb/utils/distance.h"

#include <random>
#include <limits>
#include <set>
#include <algorithm>
#include <vector>
#include <queue>
#include <span>
#include <mutex>


namespace tamdb {

HNSWIndex::HNSWIndex(uint32_t M, uint32_t ef_construction, size_t dimensions) 
    : _M(M), 
    _ef_construction(ef_construction), 
    _dimensions(dimensions),
    _rng(std::random_device{}()),
    _level_dist(0.0, 1.0)
{}


HNSWIndex::HNSWIndex(IndexMetadata& index_md, std::vector<float>& flat_vectors, std::vector<HNSWNode>& nodes) 
    : _M(index_md.m), 
    _ef_construction(index_md.ef_construction), 
    _dimensions(index_md.dimensions),
    _rng(std::random_device{}()),
    _level_dist(0.0, 1.0),
    _root(index_md.root_entry_point),
    _flat_vectors(flat_vectors),
    _nodes(nodes)
    {}

void HNSWIndex::insert(uint64_t id, std::span<const float> input_vector) {
    std::unique_lock lock(_mutex);
    if(input_vector.size() != _dimensions)
        throw std::invalid_argument("dimension mismatch");
    

    // find the offset and insert the new vector
    uint32_t insert_node_idx = _nodes.size();
    for(size_t i = 0; i < input_vector.size(); ++i) {
        _flat_vectors.push_back(input_vector[i]);
    }

    uint32_t insert_node_layer = random_layer();
    std::vector<std::vector<uint32_t>> adj_list;
        for(int i = 0; i <= insert_node_layer; ++i) {
        adj_list.push_back(std::vector<uint32_t>{});
    }

    _nodes.push_back({id, std::move(adj_list)});
    
    if(_root == HNSWIndex::EMPTY_ROOT) {
        _root = insert_node_idx;
        return; 
    }


    std::vector<CandidateNode> layer_cand_nodes = 
                        {{_root, l2_distance(input_vector, 
                                std::span<const float>{_flat_vectors.data() + _root * _dimensions, _dimensions})}};
    for(int32_t curr_layer = _nodes[_root].adj_list.size() - 1; curr_layer >= 0; --curr_layer) {
        if(curr_layer > insert_node_layer) {
            // zoom phase
            layer_cand_nodes = search_layer(layer_cand_nodes, 1, curr_layer, input_vector);
        } else {
            // connect phase
            layer_cand_nodes = search_layer(layer_cand_nodes, _ef_construction, curr_layer, input_vector);
            connect_layer(insert_node_idx, curr_layer, layer_cand_nodes);
        }
    }
        
    // Current node sits higher than root, so it becomes the new root.
    if(insert_node_layer > _nodes[_root].adj_list.size() - 1) {
        _root = insert_node_idx;
    }
}

void HNSWIndex::connect_layer(const uint32_t insert_node_idx, const uint32_t layer, 
                        const std::vector<CandidateNode>& possible_neighbours) {

    uint32_t max_edges = (layer != 0) ? std::min((uint32_t)possible_neighbours.size(), _M) 
                                : std::min((uint32_t)possible_neighbours.size(), 2 * _M);
    for(int i = 0; i < max_edges; ++i) {
        // Connect both nodes with each other
        auto& v_adj_list = _nodes[possible_neighbours[i].node_idx].adj_list[layer];
        
        _nodes[insert_node_idx].adj_list[layer].push_back(possible_neighbours[i].node_idx);
        v_adj_list.push_back(insert_node_idx);
        

        if((v_adj_list.size() > _M && layer != 0) || (v_adj_list.size() > 2 * _M && layer == 0)) {
            prune_edge(possible_neighbours[i].node_idx, layer);
        }
    }
}

void HNSWIndex::prune_edge(const uint32_t node_idx, uint32_t layer) {

    uint32_t farthest_neighbour = _nodes[node_idx].adj_list[layer][0];
    float max_distance = l2_distance(std::span<const float>{
                                            _flat_vectors.data() + node_idx * _dimensions,
                                            _dimensions},
                                    std::span<const float>{
                                            _flat_vectors.data() + _nodes[node_idx].adj_list[layer][0] * _dimensions,
                                            _dimensions}
                                    ); 
    for(auto& v_idx : _nodes[node_idx].adj_list[layer]) {
        float dist = l2_distance(std::span<const float> {_flat_vectors.data() + node_idx * _dimensions, _dimensions},
                                std::span<const float> {_flat_vectors.data() + v_idx * _dimensions, _dimensions});
        
        if(max_distance < dist) {
            max_distance = dist;
            farthest_neighbour = v_idx;
        }
    }

    _nodes[node_idx].adj_list[layer].erase(std::remove(_nodes[node_idx].adj_list[layer].begin(), 
                                                        _nodes[node_idx].adj_list[layer].end(), 
                                                        farthest_neighbour),
                                            _nodes[node_idx].adj_list[layer].end());

    _nodes[farthest_neighbour].adj_list[layer].erase(std::remove(_nodes[farthest_neighbour].adj_list[layer].begin(), 
                                                        _nodes[farthest_neighbour].adj_list[layer].end(), 
                                                        node_idx),
                                            _nodes[farthest_neighbour].adj_list[layer].end());

}

std::vector<CandidateNode> HNSWIndex::search_layer(const std::vector<CandidateNode> entry_points, const uint32_t EF, 
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
    for(const CandidateNode& cand_node: entry_points) {
        uint32_t u_idx = cand_node.node_idx;
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

std::vector<SearchResult> HNSWIndex::search(std::span<const float> query_vector, size_t pop_k, size_t ef_search) {

    std::shared_lock lock(_mutex);
    if(_root == HNSWIndex::EMPTY_ROOT)
        return std::vector<SearchResult>{};

    std::vector<CandidateNode> layer_cand_nodes = {{_root, l2_distance(
                                                        query_vector,
                                                        std::span<const float>{_flat_vectors.data() + _root * _dimensions, _dimensions})}};
    
    // Zoom Phase Loop                                
    for(int layer = _nodes[_root].adj_list.size() - 1; layer > 0; --layer) {
        layer_cand_nodes = search_layer(layer_cand_nodes, 1, layer, query_vector);
    }
    // Search Phase - looks for the node in the layer 0
    layer_cand_nodes = search_layer(layer_cand_nodes, ef_search, 0, query_vector);

    std::vector<SearchResult> search_nodes_results;
    size_t n = std::min(pop_k, layer_cand_nodes.size());
    for(int i = 0; i < n; ++i) {
        search_nodes_results.push_back({ _nodes[layer_cand_nodes[i].node_idx]._vector_id, layer_cand_nodes[i].node_dist});
    }

    return search_nodes_results;
}


uint32_t HNSWIndex::random_layer() {
    return std::floor( (-1) * std::log(_level_dist(_rng)) * (1 / std::log(_M)));
}

IndexMetadata HNSWIndex::getIndexMetadata() {
    return IndexMetadata {
        .magic_number = 0x484E5357,
        .dimensions = _dimensions,
        .m = _M,
        .ef_construction = _ef_construction,
        .node_count = _nodes.size(),
        .root_entry_point = _root
    };
}

const std::vector<HNSWNode>& HNSWIndex::getNodes() {
    return _nodes;
}

const std::vector<float>& HNSWIndex::getFlatVectors() {
    return _flat_vectors;
}
};
