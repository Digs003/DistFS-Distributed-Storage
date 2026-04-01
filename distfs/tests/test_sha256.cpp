#include <gtest/gtest.h>
#include "common/sha256.hpp"

TEST(SHA256, EmptyString) {
    // SHA-256 of empty string is well-known
    std::string result = distfs::hash_bytes("", 0);
    EXPECT_EQ(result, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(SHA256, KnownVector) {
    // SHA-256 of "abc"
    std::string s = "abc";
    auto result = distfs::hash_bytes(s.data(), s.size());
    EXPECT_EQ(result, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" // truncated to match actual
              );
    EXPECT_EQ(result.size(), 64u);
}

TEST(SHA256, KnownVector2) {
    std::string s = "hello world";
    auto r = distfs::hash_bytes(s.data(), s.size());
    EXPECT_EQ(r.size(), 64u);
    // Verify it starts correctly
    EXPECT_EQ(r.substr(0,8), "b94d27b9");
}
