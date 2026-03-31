#include "common/sha256.hpp"
#include <openssl/evp.h>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <memory>

namespace distfs {

static std::string to_hex(const unsigned char* digest, unsigned int len) {
    std::ostringstream ss;
    for (unsigned int i = 0; i < len; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    return ss.str();
}

std::string hash_bytes(const void* data, std::size_t len) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");

    auto cleanup = [&](EVP_MD_CTX* c){ EVP_MD_CTX_free(c); };
    std::unique_ptr<EVP_MD_CTX, decltype(cleanup)> guard(ctx, cleanup);

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data, len) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        throw std::runtime_error("SHA-256 computation failed");
    }

    return to_hex(digest, digest_len);
}

std::string hash_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open file for hashing: " + path);

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");
    auto cleanup = [&](EVP_MD_CTX* c){ EVP_MD_CTX_free(c); };
    std::unique_ptr<EVP_MD_CTX, decltype(cleanup)> guard(ctx, cleanup);

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("EVP_DigestInit_ex failed");

    char buf[65536];
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
        if (EVP_DigestUpdate(ctx, buf, file.gcount()) != 1)
            throw std::runtime_error("EVP_DigestUpdate failed");
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1)
        throw std::runtime_error("EVP_DigestFinal_ex failed");

    return to_hex(digest, digest_len);
}

} // namespace distfs
