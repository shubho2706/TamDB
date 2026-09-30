#pragma once

namespace tamdb{

/** 
* Identifies the operational role assigned to a tamDB cluster node at startup. 
*/
enum NodeRole {
    DATA,         /* Stores vectors and services HNSW index reads/writes. */
    COORDINATOR,  /* Routes client requests across data nodes and aggregates results. */
    MASTER,       /* Manages cluster membership and metadata. */
    COMMON_NODE   /* Default fallback role when no specific role is configured. */
};

}
