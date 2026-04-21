#include "raft/raft_log.hpp"
#include <stdexcept>
#include <cstring>
#include <fstream>

namespace distfs {

// ---- Wire format for one log entry in WAL ----
// [ term: int64_t (8 bytes LE) ][ command_len: uint32_t (4 bytes LE) ][ command: bytes ]

std::vector<uint8_t> RaftLog::serialize(const RaftLogEntry& e) {
    std::vector<uint8_t> buf;
    buf.resize(8 + 4 + e.command.size());
    int64_t  term = e.term;
    uint32_t cmdlen = static_cast<uint32_t>(e.command.size());
    std::memcpy(buf.data(),     &term,   8);
    std::memcpy(buf.data() + 8, &cmdlen, 4);
    if (!e.command.empty())
        std::memcpy(buf.data() + 12, e.command.data(), e.command.size());
    return buf;
}

RaftLogEntry RaftLog::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 12) throw std::runtime_error("RaftLog: corrupt entry");
    RaftLogEntry e;
    std::memcpy(&e.term, data.data(), 8);
    uint32_t cmdlen = 0;
    std::memcpy(&cmdlen, data.data() + 8, 4);
    if (data.size() < 12 + cmdlen) throw std::runtime_error("RaftLog: truncated command");
    e.command.assign(data.begin() + 12, data.begin() + 12 + cmdlen);
    return e;
}

RaftLog::RaftLog(const std::string& wal_path) : wal_path_(wal_path) {
    // Replay WAL to reconstruct entries_
    WAL wal(wal_path_);
    wal.replay([&](const std::vector<uint8_t>& data) {
        try {
            entries_.push_back(deserialize(data));
        } catch (...) {
            // Fail fast on replay corruption.
            throw std::runtime_error("RaftLog: corrupt entry");
        }
    });
}

int64_t RaftLog::append(const RaftLogEntry& entry) {
    WAL wal(wal_path_);
    wal.append(serialize(entry));
    entries_.push_back(entry);
    return static_cast<int64_t>(entries_.size()); // 1-based
}

RaftLogEntry RaftLog::get(int64_t index) const {
    if (index < 1 || index > static_cast<int64_t>(entries_.size()))
        throw std::out_of_range("RaftLog::get: index " + std::to_string(index) + " out of range");
    return entries_[static_cast<size_t>(index - 1)];
}

void RaftLog::truncate(int64_t from_index) {
    if (from_index < 1) return;
    if (from_index <= static_cast<int64_t>(entries_.size()))
        entries_.erase(entries_.begin() + (from_index - 1), entries_.end());
    rewrite_wal();
}

int64_t RaftLog::last_index() const {
    return static_cast<int64_t>(entries_.size());
}

int64_t RaftLog::last_term() const {
    if (entries_.empty()) return 0;
    return entries_.back().term;
}

void RaftLog::rewrite_wal() {
    // Truncate WAL file and rewrite all surviving entries
    // We open for write+truncate to clear existing content
    {
        std::ofstream f(wal_path_, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("RaftLog: cannot truncate WAL: " + wal_path_);
    }
    WAL wal(wal_path_);
    for (const auto& e : entries_)
        wal.append(serialize(e));
}

} // namespace distfs
