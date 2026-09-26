"""
tamDB E2E Test: Persist and recover.

Tests that the server survives a restart by:
1. Starting the server in a tmux window
2. Inserting vectors
3. Killing the server
4. Restarting the server
5. Searching without re-inserting — verifying data survived

Prerequisites:
    pip install grpcio grpcio-tools
    python3 -m grpc_tools.protoc -Iproto --python_out=integ_tests --grpc_python_out=integ_tests proto/tamdb.proto

Usage (from tamdb/ root):
    python3 integ_tests/test_persist.py
"""

import grpc
import os
import subprocess
import sys
import time
import tamdb_pb2
import tamdb_pb2_grpc

DIMS = 128
PROJECT_DIR = "<TODO: Path to project>"
SERVER_BIN = f"{PROJECT_DIR}/build/tamdb_server"
TMUX_SESSION = "tamdb_e2e"
WAL_FILE = f"{PROJECT_DIR}/tamdb_wal.bin"


def tmux_cmd(cmd):
    subprocess.run(["tmux", "send-keys", "-t", TMUX_SESSION, cmd, "Enter"], check=True)


def start_server():
    # Kill existing session if any
    subprocess.run(["tmux", "kill-session", "-t", TMUX_SESSION], capture_output=True)
    # Create new tmux session
    subprocess.run(["tmux", "new-session", "-d", "-s", TMUX_SESSION], check=True)
    tmux_cmd(f"cd {PROJECT_DIR} && {SERVER_BIN}")
    print("  Waiting for server to start...")
    time.sleep(3)


def stop_server():
    tmux_cmd("C-c")
    time.sleep(1)
    subprocess.run(["tmux", "kill-session", "-t", TMUX_SESSION], capture_output=True)


def cleanup_wal():
    if os.path.exists(WAL_FILE):
        os.remove(WAL_FILE)


def get_stub():
    channel = grpc.insecure_channel("localhost:50051")
    return tamdb_pb2_grpc.TamDBServiceStub(channel)


def insert_vectors(stub, count=10):
    print(f"  Inserting {count} vectors...")
    for i in range(count):
        vec = [float(i) / count] * DIMS
        res = stub.InsertVector(tamdb_pb2.InsertRequest(id=i, input_vector=vec))
        if not res.ok:
            print(f"  FAIL: insert id={i} failed: {res.error}")
            return False
    print(f"  OK: {count} vectors inserted")
    return True


def search_and_verify(stub, expected_id, query_vec, label=""):
    res = stub.SearchVector(tamdb_pb2.SearchRequest(
        search_vector=query_vec, top_k=1, ef_search=200
    ))
    if len(res.search_results) == 0:
        print(f"  FAIL {label}: no results")
        return False
    top = res.search_results[0]
    if top.id == expected_id:
        print(f"  PASS {label}: id={top.id} dist={top.score:.4f}")
        return True
    else:
        print(f"  FAIL {label}: expected id={expected_id}, got id={top.id}")
        return False


def main():
    print("=== tamDB E2E: Persist & Recover ===\n")

    # Clean slate
    print("[1] Cleanup")
    cleanup_wal()
    print("  Removed old WAL file\n")

    # Start server
    print("[2] Starting server")
    start_server()
    stub = get_stub()

    # Insert
    print("[3] Inserting vectors")
    if not insert_vectors(stub, 10):
        stop_server()
        sys.exit(1)

    # Verify pre-restart
    print("\n[4] Verify before restart")
    search_and_verify(stub, 3, [0.3] * DIMS, "pre-restart id=3")

    # Kill server
    print("\n[5] Killing server")
    stop_server()
    print("  Server stopped\n")

    # Restart server
    print("[6] Restarting server (WAL replay)")
    start_server()
    stub = get_stub()

    # Verify post-restart
    print("[7] Verify after restart (no inserts)")
    passed = 0
    total = 3
    if search_and_verify(stub, 3, [0.3] * DIMS, "id=3"):
        passed += 1
    if search_and_verify(stub, 0, [0.0] * DIMS, "id=0"):
        passed += 1
    if search_and_verify(stub, 9, [0.9] * DIMS, "id=9"):
        passed += 1

    # Cleanup
    print("\n[8] Cleanup")
    stop_server()
    cleanup_wal()

    print(f"\n{'PASS' if passed == total else 'FAIL'}: {passed}/{total} tests passed")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
