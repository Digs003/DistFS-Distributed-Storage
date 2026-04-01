#include "client/chunker.hpp"
#include "common/sha256.hpp"
#include <fstream>
#include <stdexcept>

namespace distfs {

std::vector<LocalChunkInfo> split_file(const std::string& path, int64_t chunk_size) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open file for chunking: " + path);

    std::vector<LocalChunkInfo> chunks;
    std::vector<char> buf(chunk_size);
    int32_t idx = 0;

    while (true) {
        file.read(buf.data(), chunk_size);
        auto bytes_read = file.gcount();
        if (bytes_read == 0) break;

        LocalChunkInfo ci;
        ci.chunk_index = idx++;
        ci.size_bytes  = bytes_read;
        ci.chunk_hash  = hash_bytes(buf.data(), static_cast<size_t>(bytes_read));
        chunks.push_back(std::move(ci));

        if (bytes_read < chunk_size) break; // last chunk
    }
    return chunks;
}

std::string reassemble_file(const std::vector<std::string>& chunk_tmp_paths,
                             const std::string& out_path) {
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot open output file: " + out_path);

    for (const auto& cp : chunk_tmp_paths) {
        std::ifstream in(cp, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open chunk temp file: " + cp);
        out << in.rdbuf();
    }
    out.close();
    return hash_file(out_path);
}

} // namespace distfs
