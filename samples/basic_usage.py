"""
tamDB Sample: Insert and Search vectors over gRPC.

Prerequisites:
    pip install grpcio grpcio-tools
    python3 -m grpc_tools.protoc -Iproto --python_out=samples --grpc_python_out=samples proto/tamdb.proto

Usage:
    1. Start the server:  ./build/tamdb_server
    2. Run this script:   python3 samples/basic_usage.py
"""

import grpc
import tamdb_pb2
import tamdb_pb2_grpc

def main():
    channel = grpc.insecure_channel("localhost:50051")
    stub = tamdb_pb2_grpc.TamDBServiceStub(channel)

    # --- Insert 10 vectors (128 dimensions) ---
    print("=== Inserting 10 vectors ===")
    vectors = {
        0: [0.1] * 128,
        1: [0.2] * 128,
        2: [0.3] * 128,
        3: [0.4] * 128,
        4: [0.5] * 128,
        5: [0.6] * 128,
        6: [0.7] * 128,
        7: [0.8] * 128,
        8: [0.9] * 128,
        9: [1.0] * 128,
    }

    for vid, vec in vectors.items():
        res = stub.InsertVector(tamdb_pb2.InsertRequest(id=vid, input_vector=vec))
        status = "ok" if res.ok else f"FAILED: {res.error}"
        print(f"  id={vid}: {status}")

    # --- Search for nearest neighbors ---
    print("\n=== Search: query close to id=3 ([0.4]*128) ===")
    query = [0.41] * 128
    res = stub.SearchVector(tamdb_pb2.SearchRequest(
        search_vector=query, top_k=3, ef_search=200
    ))
    for r in res.search_results:
        print(f"  id={r.id}  dist={r.score:.4f}")

    # --- Search for a different region ---
    print("\n=== Search: query close to id=9 ([1.0]*128) ===")
    query = [0.99] * 128
    res = stub.SearchVector(tamdb_pb2.SearchRequest(
        search_vector=query, top_k=3, ef_search=200
    ))
    for r in res.search_results:
        print(f"  id={r.id}  dist={r.score:.4f}")

if __name__ == "__main__":
    main()
