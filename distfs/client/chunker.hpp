#pragma once
#include "common/types.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace distfs {

// Split a file into 4 MB SHA-256-named chunks.
std::vector<LocalChunkInfo> split_file(const std::string& path,
                                   int64_t chunk_size = 64 * 1024 * 1024);

// Concatenate temporary chunk files in order and write to out_path.
// Returns SHA-256 hash of the reassembled file for verification.
std::string reassemble_file(const std::vector<std::string>& chunk_tmp_paths,
                             const std::string& out_path);

} // namespace distfs
