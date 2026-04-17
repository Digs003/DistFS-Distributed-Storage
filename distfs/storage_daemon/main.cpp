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
    std::string data_dir = "/var/distfs/chunks";
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg.rfind("--id=",0)==0)       node_id  = arg.substr(5);
        else if (arg.rfind("--port=",0)==0)      port     = arg.substr(7);
        else if (arg.rfind("--data-dir=",0)==0)  data_dir = arg.substr(11);
        else if (arg.rfind("--config=",0)==0)    config_path = arg.substr(9);
        else if (arg == "--verbose" || arg == "-v") verbose = true;
    }

    if (node_id.empty() || port.empty()) {
        std::cerr << "Usage: storage_daemon --id=<id> --port=<port> "
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

    // We MUST bind to 0.0.0.0 so we can receive requests from any IP.
    std::string listen_addr = "0.0.0.0:" + port;

    // We MUST report our _routable_ IP to the metadata server, otherwise 
    // clients and other daemons will try to connect to 0.0.0.0
    // Try to find our routable IP from the config file's storage.daemons list.
    std::string report_addr = "127.0.0.1:" + port; // fallback config
    for (auto& d : cfg.storage.daemons) {
        if (d.id == node_id) {
            report_addr = d.address;
            break;
        }
    }

    // ---- Start heartbeat client ----
    distfs::HeartbeatClient hb(node_id, report_addr, cfg.client.metadata_nodes,
                                cfg.storage.dead_threshold_sec / 3);
    hb.start();

    // ---- Start gRPC server ----
    distfs::StorageServiceImpl service(data_dir, node_id);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen_addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    if (!server) {
        std::cerr << "[ERROR] Failed to start gRPC server on " << listen_addr << "\n";
        return 1;
    }
    std::cout << "[storage_daemon] " << node_id << " listening on " << listen_addr
              << " (reporting as " << report_addr << ") | data_dir=" << data_dir << "\n";

    // ---- Graceful shutdown ----
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);
    while (!g_shutdown) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "[storage_daemon] Shutting down...\n";
    hb.stop();
    server->Shutdown();
    return 0;
}
