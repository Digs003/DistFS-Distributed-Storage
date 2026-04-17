#include "common/wal.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>

namespace distfs {

namespace {
    void checked_write(int fd, const void* buf, size_t n) {
        const char* p = reinterpret_cast<const char*>(buf);
        size_t remaining = n;
        while (remaining > 0) {
            ssize_t written = ::write(fd, p, remaining);
            if (written < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(std::string("WAL write error: ") + strerror(errno));
            }
            p += written;
            remaining -= written;
        }
    }
}

WAL::WAL(const std::string& path) : path_(path) {
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0)
        throw std::runtime_error(std::string("WAL open failed: ") + strerror(errno) + " (" + path + ")");
}

WAL::~WAL() {
    if (fd_ >= 0) ::close(fd_);
}

void WAL::append(const std::vector<uint8_t>& data) {
    uint32_t len = static_cast<uint32_t>(data.size());
    checked_write(fd_, &len, sizeof(len));
    if (!data.empty())
        checked_write(fd_, data.data(), data.size());
    if (::fsync(fd_) != 0)
        throw std::runtime_error(std::string("WAL fsync failed: ") + strerror(errno));
}

void WAL::append(const std::string& data) {
    std::vector<uint8_t> buf(data.begin(), data.end());
    append(buf);
}

void WAL::replay(const std::function<void(const std::vector<uint8_t>&)>& callback) const {
    int rfd = ::open(path_.c_str(), O_RDONLY);
    if (rfd < 0) {
        // File doesn't exist yet — empty log, nothing to replay
        if (errno == ENOENT) return;
        throw std::runtime_error(std::string("WAL replay open failed: ") + strerror(errno));
    }

    auto close_guard = [&](){ ::close(rfd); };

    uint32_t len = 0;
    while (true) {
        ssize_t r = ::read(rfd, &len, sizeof(len));
        if (r == 0) break;           // clean EOF
        if (r != sizeof(len)) break; // partial header — truncated, stop here

        std::vector<uint8_t> buf(len);
        size_t read_total = 0;
        bool partial = false;
        while (read_total < len) {
            ssize_t got = ::read(rfd, buf.data() + read_total, len - read_total);
            if (got <= 0) { partial = true; break; }
            read_total += got;
        }
        if (partial) break; // partial entry at end — skip (crash-safe)

        callback(buf);
    }

    close_guard();
}

std::size_t WAL::entry_count() const {
    std::size_t count = 0;
    replay([&](const std::vector<uint8_t>&){ ++count; });
    return count;
}

} // namespace distfs
