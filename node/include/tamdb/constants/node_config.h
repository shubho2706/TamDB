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

/** Configuration for a coordinator node, listing the data nodes it routes requests to. */
struct CoordNodeConfig {
    std::vector<NodeAddress> data_nodes;
};

/** HNSW index construction and capacity parameters for a data node. */
struct DataNodeConfig {
    uint32_t max_connections_per_node_per_layer{16};
    uint32_t ef_construction{200};
    uint32_t dimensions{128};
};

/** Top-level cluster configuration; carries the role-specific sub-config for the running node. */
struct ClusterConfig {
    std::optional<DataNodeConfig> data_node_config;
    std::optional<CoordNodeConfig> coord_node_config;
};


}
