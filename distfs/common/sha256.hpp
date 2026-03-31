#pragma once
#include <string>
#include <cstddef>

namespace distfs {

/// Compute SHA-256 of a file on disk.
/// Returns a 64-char lowercase hex string, or throws std::runtime_error.
std::string hash_file(const std::string& path);

/// Compute SHA-256 of a buffer in memory.
/// Returns a 64-char lowercase hex string.
std::string hash_bytes(const void* data, std::size_t len);

} // namespace distfs
