#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace distfs {

struct RaftConfig {
    struct Node {
        std::string id;
        std::string address; // "ip:port"
    };
    std::vector<Node> nodes;
    int election_timeout_min_ms = 150;
    int election_timeout_max_ms = 300;
    int heartbeat_interval_ms   = 50;
    int rpc_timeout_ms          = 100;
};

struct StorageConfig {
    struct Daemon {
        std::string id;
        std::string address;
    };
    std::vector<Daemon> daemons;
    int64_t chunk_size_bytes   = 4194304; // 4 MB
    int     replication_factor = 2;
    int     dead_threshold_sec = 9;
};

struct ClientConfig {
    std::vector<std::string> metadata_nodes; // "ip:port" list
};

struct ClusterConfig {
    RaftConfig    raft;
    StorageConfig storage;
    ClientConfig  client;
};

/// Parse an INI-style cluster.conf file.
/// Throws std::runtime_error on parse failures.
ClusterConfig parse_config(const std::string& path);

} // namespace distfs
