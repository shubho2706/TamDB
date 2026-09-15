#pragma once

#include "tamdb/hnsw/hnsw_index.h"

#include <cstdint>
#include <string>
#include <span>

namespace tamdb {

class WriteAheadLogger {

public: 
    WriteAheadLogger(const std::string& log_file_path);
    
    bool write(uint64_t id, std::span<const float> input_vector);
    ~WriteAheadLogger();

    bool replay(HNSWIndexPtr hnswPtr);

private:    
    bool init();
    bool append(uint64_t id, std::span<const float> input_vector);
    bool sync();

    // file descriptor
    int _fd;
    std::string _log_file_path;

};
typedef std::shared_ptr<WriteAheadLogger> WriteAheadLoggerPtr;
}

