#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace distfs {

/// Content-Addressable chunk storage on the local ext4 filesystem.
/// Chunks stored at: <data_dir>/<2-char-prefix>/<sha256>.bin
class ChunkStore {
public:
    explicit ChunkStore(const std::string& data_dir);

    /// Write chunk bytes to disk (mkdir on demand, fsync).
    /// Idempotent if the chunk already exists.
    void write_chunk(const std::string& hash, const std::vector<uint8_t>& data);

    /// Read chunk from disk. Throws if not found.
    std::vector<uint8_t> read_chunk(const std::string& hash) const;

    /// True if <hash>.bin exists on disk.
    bool has_chunk(const std::string& hash) const;

    /// Unlink the chunk file. No-op if not found.
    void delete_chunk(const std::string& hash);

    /// Directory where chunks are stored.
    const std::string& data_dir() const { return data_dir_; }

    struct Stats {
        int64_t used_bytes;
        int64_t total_bytes;
        int64_t chunk_count;
    };

    /// Calculate current usage stats by scanning the data directory.
    Stats get_stats() const;

private:
    std::string data_dir_;
    std::string chunk_path(const std::string& hash) const;
};

} // namespace distfs
