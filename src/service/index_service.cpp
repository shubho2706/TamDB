#include "tamdb/service/index_service.h"
#include <exception>

namespace tamdb {
    IndexService::IndexService(HNSWIndexPtr hnswIndexPtr) : _hnswIndexPtr(hnswIndexPtr) {

    }

    bool IndexService::insertVector(uint64_t id, const std::span<const float> input_vector) {
        try {
            _hnswIndexPtr->insert(id, input_vector);
        } catch(std::exception& ex) {
            return false;
        }
        
        return true;
    }
}