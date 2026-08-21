#pragma once

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace tamdb {

struct SearchResult {
    uint64_t id;
    float score;
};

class FlatIndex {
public:
    explicit FlatIndex(size_t dimensions);

    void insert(uint64_t id, std::span<const float> vector);
    std::vector<SearchResult> search(std::span<const float> query, size_t top_k) const;
    size_t size() const;

private:
    size_t _dimensions;
    std::vector<uint64_t> _ids;
    std::vector<float> _vectors; // contiguous: [vec0_dim0, vec0_dim1, ..., vec1_dim0, ...]
};

} // namespace tamdb
