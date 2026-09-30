# tamDB

A vector database written from scratch in C++20.

Built for learning — exploring how vector search engines work under the hood, from distance functions to HNSW graphs to distributed systems.

## What it does

- **HNSW index** — approximate nearest neighbor search with configurable M, ef_construction, and ef_search
- **gRPC API** — insert and search vectors over the network
- **Persistence** — write-ahead log for crash recovery, binary segment serialization
- **Distributed** — coordinator node with scatter-gather search across data node shards

## Quick start

```bash
# Build
cmake --preset cd-1 && cmake --build build

# Run (single node, all roles)
./build/tamdb_node --port 50051 --config cluster.json

# Run (multi-node cluster)
./build/tamdb_node --roles data --port 50051 --config cluster.json
./build/tamdb_node --roles data --port 50052 --config cluster.json
./build/tamdb_node --roles coordinator --port 50050 --config cluster.json
```

## Status

Work in progress. This is a personal project for learning systems programming.
