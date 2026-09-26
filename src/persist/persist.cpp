#include "tamdb/hnsw/hnsw_index.h"
#include "tamdb/persist/persist.h"

#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <cstdio>

namespace tamdb {

Persist::Persist(const std::string& file_path) : _file_path(file_path) {

}

bool Persist::write(HNSWIndexPtr hnsw_index_ptr) {
    // we need to block write op here using the index 

    int fd = ::open(_file_path.c_str(), O_WRONLY | O_CREAT, 0644);
    if(fd == -1) {
        std::perror("Unable to open file");
        return false;
    }

    struct iovec iov[4];
    
    // 1. Store Index Metadata
    IndexMetadata index_md = hnsw_index_ptr->getIndexMetadata();
    iov[0].iov_base = &index_md;
    iov[0].iov_len = sizeof(IndexMetadata);

    // 2. Store the vector IDs
    std::vector<HNSWNode> nodes = hnsw_index_ptr->getNodes();
    std::vector<uint64_t> vector_ids;
    vector_ids.reserve(nodes.size());
    for(const auto& node : nodes) {
        vector_ids.push_back(node._vector_id);
    }
    iov[1].iov_base = const_cast<uint64_t*>(vector_ids.data());
    iov[1].iov_len = index_md.node_count * sizeof(uint64_t);

    // 3. Store the float vectors
    const std::vector<float> flat_vector = hnsw_index_ptr->getFlatVectors();
    iov[2].iov_base = const_cast<float*>(flat_vector.data());
    iov[2].iov_len = flat_vector.size() * sizeof(float);

    // 4. Now Build and store the graphs/ adj list to store
    std::vector<uint32_t> flat_frozen_graph;
    for (const HNSWNode& node: nodes) {
        const std::vector<std::vector<uint32_t>>& adj_list = node.adj_list;
        flat_frozen_graph.push_back(adj_list.size());
        for(const std::vector<uint32_t>& layer_nodes: adj_list) {
            flat_frozen_graph.push_back(layer_nodes.size());
            flat_frozen_graph.insert(flat_frozen_graph.end(), layer_nodes.begin(), layer_nodes.end());
        }
    }
    iov[3].iov_base = const_cast<uint32_t*>(flat_frozen_graph.data());
    iov[3].iov_len = flat_frozen_graph.size() * sizeof(uint32_t);

    ssize_t total_bytes_expected = iov[0].iov_len + iov[1].iov_len 
                                + iov[2].iov_len + iov[3].iov_len;
    ssize_t total_bytes_written = ::writev(fd, iov, 4);
    if(total_bytes_written != total_bytes_expected) {
        std::perror("Error writing binary data");
        ::close(fd);
        return false;
    }
    ::close(fd);
    return true;
}


bool Persist::read(HNSWIndexPtr& hnsw_index_ptr) {

    int fd = ::open(_file_path.c_str(), O_RDONLY);
    if(fd == -1) {
        std::perror("Error Opening File");
        return false;
    }

    // TODO: 0. Read the Magic Number and ensure the file is correct

    // 1. Read the Index Metadata
    IndexMetadata index_md;
    ssize_t bytes_read = ::read(fd, &index_md, sizeof(IndexMetadata));
    if(bytes_read == -1) {
        std::perror("Failed to read Index MD");
        ::close(fd);
        return false;
    }

    // 2. Read the vector IDs
    std::vector<uint64_t> vector_ids(index_md.node_count);
    ssize_t expected_bytes_read = sizeof(uint64_t) * index_md.node_count;
    bytes_read = ::read(fd, vector_ids.data(), expected_bytes_read); 
    if(bytes_read != expected_bytes_read) {
        std::perror("Failed to read Vector IDs");
        ::close(fd);
        return false;
    }

    // 3. Read the float vectors
    std::vector<float> flat_vectors(index_md.node_count * index_md.dimensions);
    expected_bytes_read = index_md.node_count * index_md.dimensions * sizeof(float);
    bytes_read = ::read(fd, flat_vectors.data(), expected_bytes_read);
    if(bytes_read != expected_bytes_read) {
        std::perror("Failed to read Vectors");
        ::close(fd);
        return false;
    }
    
    // 4. Read the graphs/ adj list to store
    std::vector<HNSWNode> nodes;
    for(uint32_t node_idx = 0; node_idx < index_md.node_count; ++node_idx) {
        // One iteration for this loop is for One node 
        uint32_t layer_count;
        ssize_t bytes_read = ::read(fd, &layer_count, sizeof(uint32_t));
        if(bytes_read == -1) {
            std::perror("Failed to read Vectors");
            ::close(fd);
            return false;
        }
        
        std::vector<std::vector<uint32_t>> adj_list;
        for(uint32_t l_idx = 0; l_idx < layer_count; ++l_idx) {
            uint32_t layer_neighbour_count;
            bytes_read = ::read(fd, &layer_neighbour_count, sizeof(uint32_t));
            if(bytes_read == -1) {
                std::perror("Failed to read Vectors");
                ::close(fd);
                return false;
            }
            
            std::vector<uint32_t> layer_neighbours(layer_neighbour_count);
            bytes_read = ::read(fd, layer_neighbours.data(), layer_neighbour_count * sizeof(uint32_t));
            if(bytes_read != layer_neighbour_count * sizeof(uint32_t)) {
                std::perror("Failed to read Vectors");
                ::close(fd);
                return false;
            }   
            adj_list.push_back(layer_neighbours);
        }

        nodes.push_back({vector_ids[node_idx], adj_list});
    }

    hnsw_index_ptr = std::make_shared<HNSWIndex>(index_md, flat_vectors, nodes);
    ::close(fd);
    return true;
}
}