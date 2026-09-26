#pragma once

#include "tamdb/hnsw/hnsw_index.h"

#include <memory>

namespace tamdb {
class Persist {

public:
    Persist(const std::string& file_path);
    bool write(HNSWIndexPtr hnsw_index_ptr);
    bool read(HNSWIndexPtr& hnsw_index_ptr);
    //~Persist();

private:
    std::string _file_path;
};

typedef std::shared_ptr<Persist> PersistPtr;

}