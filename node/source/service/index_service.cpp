#include "node/service/index_service.h"
#include <exception>

namespace tamdb {
    IndexService::IndexService(HNSWIndexPtr hnswIndexPtr, WriteAheadLoggerPtr walPtr) 
            : _hnswIndexPtr(hnswIndexPtr), _walPtr(walPtr) {}

    bool IndexService::insertVector(uint64_t id, const std::span<const float> input_vector) {
        try {
            _walPtr->write(id, input_vector);
            _hnswIndexPtr->insert(id, input_vector);
        } catch(std::exception& ex) {
            return false;
        }
        
        return true;
    }
}