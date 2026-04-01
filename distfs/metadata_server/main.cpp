#include "common/config.hpp"
#include "common/logger.hpp"
#include "metadata_server/metadata_service.hpp"
#include "metadata_server/metadata_store.hpp"
#include "raft/raft_node.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <thread>

namespace {
std::atomic<bool> g_shutdown{false};
}
static void signal_handler(int) { g_shutdown = true; }

int main(int argc, char *argv[]) {
  std::string node_id, config_path = "cluster.conf";
  int port = 5000;
  std::string wal_path;
  bool verbose = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--id=", 0) == 0)
      node_id = a.substr(5);
    else if (a.rfind("--port=", 0) == 0)
      port = std::stoi(a.substr(7));
    else if (a.rfind("--wal=", 0) == 0)
      wal_path = a.substr(6);
    else if (a.rfind("--config=", 0) == 0)
      config_path = a.substr(9);
    else if (a == "--verbose" || a == "-v")
      verbose = true;
  }
  if (node_id.empty()) {
    std::cerr << "Usage: metadata_server --id=<id> --port=<port> "
                 "[--wal=<path>] [--config=<path>] [--verbose|-v]\n";
    return 1;
  }

  distfs::Logger::instance().set_verbose(verbose);
  VLOG("metadata", "Verbose mode enabled");
  if (wal_path.empty())
    wal_path = "/var/distfs/wal_" + node_id;

  // Treat the wal parameter as a directory and append the log filename
  if (!wal_path.empty() && wal_path.back() != '/') {
    wal_path += '/';
  }
  wal_path += "raft.wal";

  distfs::ClusterConfig cfg;
  try {
    cfg = distfs::parse_config(config_path);
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] Config: " << e.what() << "\n";
    return 1;
  }

  // Build peer list (exclude self)
  std::vector<distfs::PeerInfo> peers;
  for (auto &n : cfg.raft.nodes)
    if (n.id != node_id)
      peers.push_back({n.id, n.address});

  // State machine
  distfs::MetadataStore store;

  // Raft node
  distfs::RaftNode raft(
      node_id, wal_path, peers, cfg.raft.election_timeout_min_ms,
      cfg.raft.election_timeout_max_ms, cfg.raft.heartbeat_interval_ms,
      [&](const std::vector<uint8_t> &cmd) { store.apply_command(cmd); });

  // gRPC server
  distfs::MetadataServiceImpl metadata_svc(store, raft,
                                           cfg.storage.dead_threshold_sec,
                                           cfg.storage.replication_factor);
  distfs::RaftServiceImpl raft_svc(raft);

  std::string addr = "0.0.0.0:" + std::to_string(port);
  grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&metadata_svc);
  builder.RegisterService(&raft_svc);
  auto server = builder.BuildAndStart();
  if (!server) {
    std::cerr << "[ERROR] Failed to start gRPC server on " << addr << "\n";
    return 1;
  }
  std::cout << "[metadata_server] " << node_id << " listening on " << addr
            << "\n";

  raft.start();
  metadata_svc.start_monitors();

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  while (!g_shutdown)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

  std::cout << "[metadata_server] Shutting down...\n";
  metadata_svc.stop_monitors();
  raft.stop();
  server->Shutdown();
  return 0;
}
