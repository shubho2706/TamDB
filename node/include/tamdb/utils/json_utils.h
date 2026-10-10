#pragma once

#include <nlohmann/json.hpp>
#include "tamdb/constants/node_config.h"

namespace tamdb {

/** Deserialises a NodeAddress from a JSON object with "ip" and "port" fields. */
inline void from_json(const nlohmann::json& j, NodeAddress& n) {
    j.at("ip").get_to(n.ip);
    j.at("port").get_to(n.port);
}

/** Deserialises a Node from a JSON object with "node_id" and "address" fields. */
inline void from_json(const nlohmann::json& j, Node& n) {
    j.at("node_id").get_to(n.node_id);
    j.at("address").get_to(n.address);
}

/** Deserialises a CoordNodeConfig from a JSON object; populates data_nodes if present. */
inline void from_json(const nlohmann::json& j, CoordNodeConfig& c) {
    if (j.contains("data_nodes")) j.at("data_nodes").get_to(c.data_nodes);
}

/** Deserialises a VectorIndexConfig  from a JSON object; all fields optional with struct defaults as fallback. */
inline void from_json(const nlohmann::json& j, VectorIndexConfig & d) {
    if (j.contains("max_connections_per_node_per_layer")) j.at("max_connections_per_node_per_layer").get_to(d.max_connections_per_node_per_layer);
    if (j.contains("ef_construction")) j.at("ef_construction").get_to(d.ef_construction);
    if (j.contains("dimensions")) j.at("dimensions").get_to(d.dimensions);
}

/** Deserialises a ClusterConfig from a JSON object; populates whichever role sub-configs are present. */
inline void from_json(const nlohmann::json& j, ClusterConfig& c) {
    if (j.contains("vector_index_config")) c.vector_index_config = j.at("vector_index_config").get<VectorIndexConfig>();
    if (j.contains("coord_node_config")) c.coord_node_config = j.at("coord_node_config").get<CoordNodeConfig>();
    if (j.contains("node_id")) j.at("node_id").get_to(c.node_id);
}

}
