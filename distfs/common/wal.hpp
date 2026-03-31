#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

namespace distfs {

/// Append-only binary Write-Ahead Log.
///
/// Wire format per entry:
///   [ length: uint32_t (LE) ][ data: <length> bytes ]
///
/// Every append() calls fsync() before returning, guaranteeing
/// that a successfully written entry survives a hard crash.
class WAL {
public:
    explicit WAL(const std::string& path);
    ~WAL();

    // Non-copyable
    WAL(const WAL&) = delete;
    WAL& operator=(const WAL&) = delete;

    /// Append a raw byte buffer as one atomic entry.
    /// Throws std::runtime_error on I/O error.
    void append(const std::vector<uint8_t>& data);
    void append(const std::string& data);

    /// Replay all entries from the beginning of the log.
    /// Calls callback(data) for each entry in order.
    /// Truncates any partial trailing entry (crash-safe).
    void replay(const std::function<void(const std::vector<uint8_t>&)>& callback) const;

    /// Returns number of complete entries in the log.
    std::size_t entry_count() const;

private:
    std::string path_;
    int fd_ = -1;          // open write descriptor
};

} // namespace distfs
