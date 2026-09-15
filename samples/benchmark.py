"""
tamDB Sample: Benchmark insert throughput and search latency over gRPC.

Prerequisites:
    pip install grpcio grpcio-tools
    python3 -m grpc_tools.protoc -Iproto --python_out=samples --grpc_python_out=samples proto/tamdb.proto

Usage:
    1. Start the server:  ./build/tamdb_server
    2. Run this script:   python3 samples/benchmark.py
"""

import grpc
import random
import time
import tamdb_pb2
import tamdb_pb2_grpc

DIMS = 128
NUM_VECTORS = 1000
NUM_QUERIES = 100
TOP_K = 10

def random_vector():
    return [random.random() for _ in range(DIMS)]

def main():
    random.seed(42)
    channel = grpc.insecure_channel("localhost:50051")
    stub = tamdb_pb2_grpc.TamDBServiceStub(channel)

    # --- Insert benchmark ---
    print(f"=== Insert {NUM_VECTORS} vectors ({DIMS}-dim) ===")
    start = time.perf_counter()
    for i in range(NUM_VECTORS):
        stub.InsertVector(tamdb_pb2.InsertRequest(id=i, input_vector=random_vector()))
    elapsed = time.perf_counter() - start
    print(f"  Total: {elapsed:.2f}s")
    print(f"  Throughput: {NUM_VECTORS / elapsed:.0f} inserts/sec")
    print(f"  Avg latency: {elapsed / NUM_VECTORS * 1000:.2f}ms/insert")

    # --- Search benchmark ---
    print(f"\n=== Search {NUM_QUERIES} queries (top-{TOP_K}) ===")
    latencies = []
    for _ in range(NUM_QUERIES):
        q = random_vector()
        start = time.perf_counter()
        stub.SearchVector(tamdb_pb2.SearchRequest(
            search_vector=q, top_k=TOP_K, ef_search=200
        ))
        latencies.append((time.perf_counter() - start) * 1000)

    latencies.sort()
    total = sum(latencies)
    print(f"  Total: {total / 1000:.2f}s")
    print(f"  Avg:   {total / len(latencies):.2f}ms")
    print(f"  p50:   {latencies[len(latencies) // 2]:.2f}ms")
    print(f"  p95:   {latencies[int(len(latencies) * 0.95)]:.2f}ms")
    print(f"  p99:   {latencies[int(len(latencies) * 0.99)]:.2f}ms")
    print(f"  QPS:   {len(latencies) / (total / 1000):.0f}")

if __name__ == "__main__":
    main()
