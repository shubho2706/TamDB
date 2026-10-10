#include "tamdb/server/grpc_server.h"
#include "tamdb/constants/node_roles.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};
void handle_signal(int /*sig*/) { g_stop.store(true); }

std::string get_flag(int argc, char** argv, const std::string& flag, const std::string& default_val) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == flag) return std::string(argv[i + 1]);
    }
    return default_val;
}

tamdb::NodeRole parse_role(const std::string& role_str) {
    if (role_str == "data") return tamdb::NodeRole::DATA;
    if (role_str == "coordinator") return tamdb::NodeRole::COORDINATOR;
    if (role_str == "master") return tamdb::NodeRole::MASTER;
    return tamdb::NodeRole::COMMON_NODE;
}

}  // namespace

int main(int argc, char** argv) {
    std::string port = get_flag(argc, argv, "--port", "50051");
    std::string role_str = get_flag(argc, argv, "--roles", "common");
    std::string config_file = get_flag(argc, argv, "--config", "cluster.json");
    std::string node_id_str = get_flag(argc, argv, "--node-id", "1");

    std::string address = "0.0.0.0:" + port;
    tamdb::NodeRole role = parse_role(role_str);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    tamdb::GRPCServer server;
    server.start(address, role, config_file);
    std::cout << "tamdb node [" << role_str << "] listening on " << address << std::endl;

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "shutting down..." << std::endl;
    server.shutdown();
    return 0;
}
