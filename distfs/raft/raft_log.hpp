#pragma once
#include "common/wal.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace distfs {

struct RaftLogEntry {
    int64_t             term;
    std::vector<uint8_t> command; // serialized metadata mutation (protobuf bytes)
};

/// Persistent Raft log backed by the WAL.
/// All entries survive crashes — the log replays from WAL on construction.
class RaftLog {
public:
    explicit RaftLog(const std::string& wal_path);

    /// Append entry. Returns the 1-based index it was assigned.
    int64_t append(const RaftLogEntry& entry);

    /// Get entry at 1-based index. Throws if out of range.
    RaftLogEntry get(int64_t index) const;

    /// Delete all entries from index onwards (inclusive). Rewrites WAL.
    void truncate(int64_t from_index);

    int64_t last_index() const;
    int64_t last_term()  const;
    int64_t size()       const { return static_cast<int64_t>(entries_.size()); }

private:
    std::string              wal_path_;
    std::vector<RaftLogEntry> entries_; // 0-based internally; index = position + 1

    // Serialize/deserialize one entry to/from bytes
    static std::vector<uint8_t> serialize(const RaftLogEntry& e);
    static RaftLogEntry         deserialize(const std::vector<uint8_t>& data);

    void rewrite_wal();
};

} // namespace distfs
