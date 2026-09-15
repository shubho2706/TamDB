#include "tamdb/wal/wal.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <string>
#include <span>
#include <cstdio>
#include <sys/uio.h>


namespace tamdb {

bool WriteAheadLogger::init() {
    if(_fd != -1)
        return true;

    _fd = ::open(_log_file_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if(_fd == -1) {
        std::perror("Error opening file"); 
        return false;
    }
    return true;
}

WriteAheadLogger::WriteAheadLogger(const std::string& log_file_path) : _log_file_path(log_file_path) {
   _fd = -1;
    this->init();
}


bool WriteAheadLogger::write(uint64_t id, std::span<const float> input_vector) {
    if(! append(id, input_vector)) 
        return false;
    return sync();
}


bool WriteAheadLogger::append(uint64_t id, std::span<const float> input_vector) {
    bool file_ready = init();
    if(! file_ready)
        return false;
    
    struct iovec iov[3];
    uint32_t vector_size = input_vector.size_bytes();
    iov[0].iov_base = &vector_size;
    iov[0].iov_len = sizeof(vector_size);

    iov[1].iov_base = &id;
    iov[1].iov_len = sizeof(id);

    iov[2].iov_base = const_cast<float*>(input_vector.data());
    iov[2].iov_len = input_vector.size_bytes();

    ssize_t total_bytes_expected = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len;
    ssize_t bytes_written = ::writev(_fd, iov, 3);

    if(bytes_written != total_bytes_expected) {
        std::perror("Error writing binary data");
        return false;
    }
    return true;
}

bool WriteAheadLogger::sync() {
    if(::fdatasync(_fd) == -1) {
        std::perror("fsync failed");
        return false;
    }
    return true;
}

bool WriteAheadLogger::replay(HNSWIndexPtr hnswPtr) {
    int fd = ::open(_log_file_path.c_str(), O_RDONLY);
    if(fd == -1) {
        std::perror("Error opening file");
        return false;
    }

    while(true) {
        uint32_t vector_size = 0;
        ssize_t bytes_read = ::read(fd, &vector_size, sizeof(uint32_t));
        if(bytes_read == 0) {
            return true; // successful replay done
        }

        if(bytes_read == -1) {
            std::perror("Failed to read size");
            ::close(fd);
            return false;
        }

        uint64_t id = 0;
        bytes_read = ::read(fd, &id, sizeof(uint64_t));
        if(bytes_read == -1) {
            std::perror("Failed to read id");
            ::close(fd);
            return false;
        }

        size_t element_count = vector_size / sizeof(float);
        std::vector<float> vector_data(element_count);
        bytes_read = ::read(fd, vector_data.data(), vector_size);
        if(bytes_read == -1 || bytes_read != vector_size) {
            std::perror("Failed to read vector");
            ::close(fd);
            return false;
        }
        hnswPtr->insert(id, std::span<const float>(vector_data.data(), vector_data.size()));
    }
}

WriteAheadLogger::~WriteAheadLogger() {
    if(_fd != -1)
        ::close(_fd);
}

}