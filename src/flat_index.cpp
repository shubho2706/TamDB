#include "tamdb/flat_index.h"
#include "tamdb/distance.h"

#include <queue>
#include <algorithm>
#include <stdexcept>

namespace tamdb {

FlatIndex::FlatIndex(size_t dimensions) : _dimensions(dimensions)
{}

void FlatIndex::insert(uint64_t id, std::span<const float> vector) {
    if (vector.size() != _dimensions) 
        throw std::invalid_argument("dimension mismatch");

    _ids.push_back(id);
    for(size_t i = 0; i < vector.size(); ++i) {
        _vectors.push_back(vector[i]);
    }
}


std::vector<SearchResult> FlatIndex::search(std::span<const float> query, size_t top_k) const {

    auto compare = [](const SearchResult& a, const SearchResult& b) {
        return a.score < b.score;
    };

    std::priority_queue<
        SearchResult,
        std::vector<SearchResult>,
        decltype(compare)> max_heap(compare);

    for(size_t i = 0; i < _ids.size(); ++i) {
        std::span<const float> curr_vec(_vectors.data() + i * _dimensions, _dimensions);

        float dist = l2_distance(query, curr_vec);

        if(max_heap.size() < top_k) {
            max_heap.push({_ids[i], dist});
        } else if (dist < max_heap.top().score){
            max_heap.pop();
            max_heap.push({_ids[i], dist});
        }
    }


    std::vector<SearchResult> results;
    
    while(! max_heap.empty()) {
        results.push_back(max_heap.top());
        max_heap.pop();
    }

    std::reverse(results.begin(), results.end());
    return results; 
}

size_t FlatIndex::size() const {
    return _ids.size();
}

} // namespace tamdb