#include "storage_daemon/chunk_store.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <fstream>

namespace distfs {

ChunkStore::ChunkStore(const std::string& data_dir) : data_dir_(data_dir) {
    // Ensure root data dir exists
    if (::mkdir(data_dir_.c_str(), 0755) != 0 && errno != EEXIST)
        throw std::runtime_error("ChunkStore: cannot create data_dir: " + std::string(strerror(errno)));
}

std::string ChunkStore::chunk_path(const std::string& hash) const {
    // /var/distfs/chunks/<prefix2>/<full-hash>.bin
    std::string prefix_dir = data_dir_ + "/" + hash.substr(0, 2);
    return prefix_dir + "/" + hash + ".bin";
}

void ChunkStore::write_chunk(const std::string& hash, const std::vector<uint8_t>& data) {
    if (has_chunk(hash)) return; // idempotent

    // Create prefix subdirectory
    std::string prefix_dir = data_dir_ + "/" + hash.substr(0, 2);
    if (::mkdir(prefix_dir.c_str(), 0755) != 0 && errno != EEXIST)
        throw std::runtime_error("ChunkStore: mkdir prefix failed: " + std::string(strerror(errno)));

    std::string path = chunk_path(hash);
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        throw std::runtime_error("ChunkStore: open for write failed: " + std::string(strerror(errno)));

    const uint8_t* ptr = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t written = ::write(fd, ptr, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            throw std::runtime_error("ChunkStore: write failed: " + std::string(strerror(errno)));
        }
        ptr += written;
        remaining -= written;
    }

    if (::fsync(fd) != 0) {
        ::close(fd);
        throw std::runtime_error("ChunkStore: fsync failed: " + std::string(strerror(errno)));
    }
    ::close(fd);
}

std::vector<uint8_t> ChunkStore::read_chunk(const std::string& hash) const {
    std::string path = chunk_path(hash);
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("ChunkStore: chunk not found: " + hash);

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buf.data()), size))
        throw std::runtime_error("ChunkStore: read failed: " + hash);
    return buf;
}

bool ChunkStore::has_chunk(const std::string& hash) const {
    struct stat st;
    return ::stat(chunk_path(hash).c_str(), &st) == 0;
}

void ChunkStore::delete_chunk(const std::string& hash) {
    std::string path = chunk_path(hash);
    if (::unlink(path.c_str()) != 0 && errno != ENOENT)
        throw std::runtime_error("ChunkStore: delete failed: " + std::string(strerror(errno)));
}

} // namespace distfs
