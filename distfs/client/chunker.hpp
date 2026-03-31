#pragma once
#include "common/types.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace distfs {

/// Split a file into 4 MB SHA-256-named chunks.
/// Returns chunks in order (chunk_index 0, 1, 2, ...).
std::vector<ChunkInfo> split_file(const std::string& path,
                                   int64_t chunk_size = 4194304);

/// Concatenate temporary chunk files in order and write to out_path.
/// Returns SHA-256 hash of the reassembled file for verification.
std::string reassemble_file(const std::vector<std::string>& chunk_tmp_paths,
                             const std::string& out_path);

} // namespace distfs
