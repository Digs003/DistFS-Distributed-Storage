#include "storage_daemon/storage_service.hpp"
#include "storage_daemon/heartbeat_client.hpp"
#include "common/config.hpp"
#include "common/logger.hpp"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <csignal>
#include <atomic>
#include <string>

namespace {
    std::atomic<bool> g_shutdown{false};
    void signal_handler(int) { g_shutdown = true; }
}

int main(int argc, char* argv[]) {
    // ---- Parse CLI args ----
    std::string config_path = "cluster.conf";
    std::string node_id;
    std::string port;
    std::string advertised_addr;
    std::string data_dir = "/var/distfs/chunks";
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg.rfind("--id=",0)==0)       node_id  = arg.substr(5);
        else if (arg.rfind("--port=",0)==0)      port     = arg.substr(7);
        else if (arg.rfind("--address=",0)==0)   advertised_addr = arg.substr(10);
        else if (arg.rfind("--data-dir=",0)==0)  data_dir = arg.substr(11);
        else if (arg.rfind("--config=",0)==0)    config_path = arg.substr(9);
        else if (arg == "--verbose" || arg == "-v") verbose = true;
    }

    if (node_id.empty() || port.empty()) {
        std::cerr << "Usage: storage_daemon --id=<id> --port=<port> [--address=<host:port>] "
                     "[--data-dir=<dir>] [--config=<path>] [--verbose|-v]\n";
        return 1;
    }

    distfs::Logger::instance().set_verbose(verbose);
    VLOG("storage", "Verbose mode enabled — node=" + node_id);

    // ---- Load config ----
    distfs::ClusterConfig cfg;
    try { cfg = distfs::parse_config(config_path); }
    catch (const std::exception& e) {
        std::cerr << "[ERROR] Config: " << e.what() << "\n";
        return 1;
    }

    std::string listen_addr = "0.0.0.0:" + port;
    if (advertised_addr.empty()) {
        advertised_addr = listen_addr;
    }

    // ---- Start heartbeat client ----
    distfs::HeartbeatClient hb(node_id, advertised_addr, cfg.client.metadata_nodes,
                                cfg.storage.dead_threshold_sec / 3);
    hb.start();

    // ---- Start gRPC server ----
    distfs::StorageServiceImpl service(data_dir, node_id);

    // ---- Start stats monitor thread ----
    std::thread stats_thread([&]() {
        distfs::ChunkStore store(data_dir);
        while (!g_shutdown) {
            auto s = store.get_stats();
            hb.update_stats(s.used_bytes, s.total_bytes, s.chunk_count);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });

    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    if (!server) {
        std::cerr << "[ERROR] Failed to start gRPC server on " << listen_addr << "\n";
        return 1;
    }
    std::cout << "[storage_daemon] " << node_id << " listening on " << listen_addr
              << " (advertised as " << advertised_addr << ") | data_dir=" << data_dir << "\n";

    // ---- Graceful shutdown ----
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);
    while (!g_shutdown) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "[storage_daemon] Shutting down...\n";
    hb.stop();
    if (stats_thread.joinable()) stats_thread.join();
    server->Shutdown();
    return 0;
}
