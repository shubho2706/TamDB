#include "tamdb/service/grpc_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};
void handle_signal(int /*sig*/) { g_stop.store(true); }

}  // namespace

int main(int argc, char** argv) {
    const std::string address = (argc > 1) ? argv[1] : "0.0.0.0:50051";

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    tamdb::GRPCServer server;
    server.start(address);
    std::cout << "tamdb server listening on " << address << std::endl;

    // Keep the process alive until a shutdown signal arrives.
    // (gRPC serves on its own threads; this loop just parks main.)
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "shutting down..." << std::endl;
    server.shutdown();
    return 0;
}
