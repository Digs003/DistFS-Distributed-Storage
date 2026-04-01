#pragma once
#include "common/types.hpp"
#include "proto_gen/distfs.pb.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace distfs {

struct FileRecord {
    std::string              filename;
    int64_t                  revision_id;
    int64_t                  total_size_bytes;
    std::vector<std::string> chunk_hashes; // ordered chunk_index
};

/// In-memory metadata state machine.
/// All mutations arrive as serialized Raft commands (apply_command).
/// Reads are lock-protected but do NOT go through Raft.
class MetadataStore {
public:
    MetadataStore() = default;

    /// Apply a committed Raft log entry to update in-memory state.
    void apply_command(const std::vector<uint8_t>& command);

    // ---- Read-path (called by gRPC handlers) ----
    bool         file_exists(const std::string& filename) const;
    FileRecord   get_file(const std::string& filename) const;
    std::vector<FileRecord> list_files() const;

    // Chunk placement helpers
    std::vector<NodeID> get_chunk_nodes(const std::string& chunk_hash) const;
    NodeID select_primary()   const;
    NodeID select_secondary(const NodeID& exclude) const;

    // Node registry (updated by RegisterHeartbeat, separate from Raft)
    void register_heartbeat(const ::distfs::HeartbeatRequest& req);
    std::vector<NodeInfo> alive_nodes() const;
    std::vector<NodeInfo> all_nodes()   const;

    /// Returns node address for given NodeID, or "" if unknown. Thread-safe.
    std::string node_address(const NodeID& id) const;

    /// Mark a node DEAD in-memory (no Raft; called on leader for quick local update). Thread-safe.
    void mark_dead(const NodeID& id);

    // Chunk stats for status command
    int64_t total_chunks()       const;
    int64_t under_replicated()   const;
    int64_t orphaned_chunks()    const;

    // Commands (serialize to bytes for Raft)
    static std::vector<uint8_t> cmd_commit_upload(
        const std::string& filename, int64_t revision_id, int64_t total_bytes,
        const std::vector<std::string>& chunk_hashes,
        const std::vector<LocalNodePlacement>& placements);

    static std::vector<uint8_t> cmd_delete_file(const std::string& filename);

    static std::vector<uint8_t> cmd_update_chunk_map(
        const std::string& chunk_hash, const std::vector<NodeID>& nodes);

private:
    mutable std::mutex                               mu_;
    std::unordered_map<std::string, FileRecord>      file_map_;
    std::unordered_map<std::string, std::vector<NodeID>> chunk_map_;
    std::unordered_map<NodeID, NodeInfo>             node_registry_;

    // Internal appliers
    void apply_commit_upload(const std::vector<uint8_t>& payload);
    void apply_delete_file(const std::vector<uint8_t>& payload);
    void apply_update_chunk_map(const std::vector<uint8_t>& payload);
};

} // namespace distfs
