"""Test distributed cluster: insert via coordinator, search via coordinator."""
import grpc
import tamdb_pb2
import tamdb_pb2_grpc

DIMS = 128
COORD_ADDR = "localhost:50050"

def main():
    channel = grpc.insecure_channel(COORD_ADDR)
    stub = tamdb_pb2_grpc.CoordNodeServiceStub(channel)

    # Insert 5 vectors via coordinator
    print("=== Insert via Coordinator ===")
    for i in range(5):
        vec = [float(i) / 5.0] * DIMS
        res = stub.InsertVector(tamdb_pb2.InsertRequest(id=i, input_vector=vec))
        status = "ok" if res.ok else f"FAILED: {res.error}"
        print(f"  id={i}: {status}")

    # Search via coordinator
    print("\n=== Search via Coordinator ===")
    query = [0.1] * DIMS  # close to id=0
    res = stub.SearchVector(tamdb_pb2.SearchRequest(
        search_vector=query, top_k=3, ef_search=200
    ))
    if len(res.search_results) == 0:
        print("  FAIL: no results")
    else:
        for r in res.search_results:
            print(f"  id={r.id}  dist={r.score:.4f}")

    # Search directly on each data node to verify fan-out
    print("\n=== Verify data on individual shards ===")
    for port in [50051, 50052]:
        ch = grpc.insecure_channel(f"localhost:{port}")
        data_stub = tamdb_pb2_grpc.DataNodeServiceStub(ch)
        res = data_stub.SearchVector(tamdb_pb2.SearchRequest(
            search_vector=query, top_k=3, ef_search=200
        ))
        print(f"  Shard :{port} — {len(res.search_results)} results")
        for r in res.search_results:
            print(f"    id={r.id}  dist={r.score:.4f}")

if __name__ == "__main__":
    main()
