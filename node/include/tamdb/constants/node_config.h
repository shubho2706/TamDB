#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

namespace tamdb {

/** Holds the IP address and port of a single cluster node. */
struct NodeAddress {
    std::string ip;
    uint16_t port{0};
};
 
struct Node {
    uint64_t node_id;
    NodeAddress address;
};

/** Configuration for a coordinator node, listing the data nodes it routes requests to. */
struct CoordNodeConfig {
    std::vector<Node> data_nodes;
};

/** HNSW index construction and capacity parameters for a data node. */
struct VectorIndexConfig  {
    uint32_t max_connections_per_node_per_layer{16};
    uint32_t ef_construction{200};
    uint32_t dimensions{128};
};

/** Top-level cluster configuration; carries the role-specific sub-config for the running node. */
struct ClusterConfig {
    std::optional<VectorIndexConfig> vector_index_config;
    std::optional<CoordNodeConfig> coord_node_config;
    uint32_t node_id;
};

/** Peer node addresses for a Raft shard group. Used by RaftOutboundClient to create stubs. */
struct RaftNodeConfig {
    std::vector<Node> peer_nodes;
};

}
