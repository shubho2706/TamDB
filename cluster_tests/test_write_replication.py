"""Test distributed write: insert via coordinator, search on data nodes."""
import grpc
import sys
sys.path.insert(0, '/local/home/bhatsh/workplace/personal/db/tamDB/repos/public/tamdb/cluster_tests')

import time
# Generate stubs first:
# cd /local/home/bhatsh/workplace/personal/db/tamDB/repos/public/tamdb
# python3 -m grpc_tools.protoc -Iproto --python_out=cluster_tests --grpc_python_out=cluster_tests proto/tamdb.proto

import tamdb_pb2
import tamdb_pb2_grpc

DIMS = 128
COORD = "localhost:50050"
DATA_NODES = ["localhost:50051", "localhost:50052", "localhost:50053"]

def main():
    # Insert via coordinator
    print("=== Insert via Coordinator ===")
    ch = grpc.insecure_channel(COORD)
    stub = tamdb_pb2_grpc.CoordNodeServiceStub(ch)

    for i in range(5):
        vec = [float(i) / 5.0] * DIMS
        res = stub.InsertVector(tamdb_pb2.InsertRequest(id=i, input_vector=vec))
        status = "ok" if res.ok else f"FAILED: {res.error}"
        print(f"  id={i}: {status}")

    print("\n--- Sleeping 10s for logs to settle ---")
    time.sleep(10)

    # Search on each data node directly
    print("\n=== Search on each data node ===")
    query = [0.1] * DIMS
    for addr in DATA_NODES:
        ch = grpc.insecure_channel(addr)
        data_stub = tamdb_pb2_grpc.DataNodeServiceStub(ch)
        res = data_stub.SearchVector(tamdb_pb2.SearchRequest(
            search_vector=query, top_k=3, ef_search=200
        ))
        count = len(res.search_results)
        print(f"  {addr}: {count} results")
        for r in res.search_results:
            print(f"    id={r.id}  dist={r.score:.4f}")

    # Search via coordinator
    print("\n=== Search via Coordinator ===")
    ch = grpc.insecure_channel(COORD)
    coord_stub = tamdb_pb2_grpc.CoordNodeServiceStub(ch)
    res = coord_stub.SearchVector(tamdb_pb2.SearchRequest(
        search_vector=query, top_k=3, ef_search=200
    ))
    for r in res.search_results:
        print(f"  id={r.id}  dist={r.score:.4f}")

if __name__ == "__main__":
    main()
