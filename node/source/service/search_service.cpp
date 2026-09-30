#include "node/service/search_service.h"

namespace tamdb {

    SearchService::SearchService(HNSWIndexPtr hnswIndexPtr) : _hnswIndexPtr(hnswIndexPtr) 
    {}

    std::vector<SearchResult> SearchService::searchVector(std::span<const float> search_vector, size_t top_k, size_t ef_search) {
        return _hnswIndexPtr->search(search_vector, top_k, ef_search);
    }
}