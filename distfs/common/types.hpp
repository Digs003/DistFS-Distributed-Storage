#pragma once
#include <string>
#include <cstdint>

namespace distfs {

// Unique identifier for a storage daemon node
using NodeID = std::string;

struct ChunkInfo {
    std::string chunk_hash;   // SHA-256 hex string
    int64_t     size_bytes;
    int32_t     chunk_index;
};

struct NodeInfo {
    std::string address;          // "ip:port"
    std::string node_id;          // daemon NodeID (e.g. "daemon-A")
    int64_t     last_heartbeat;   // Unix timestamp (seconds)
    int64_t     used_bytes;
    int64_t     total_bytes;
    int64_t     chunk_count;
    enum class Status { ALIVE, DEAD } status = Status::ALIVE;
};

struct NodePlacement {
    std::string chunk_hash;
    std::string primary_node;    // NodeID (e.g. "daemon-A")
    std::string primary_addr;
    std::string secondary_node;
    std::string secondary_addr;
};

} // namespace distfs
