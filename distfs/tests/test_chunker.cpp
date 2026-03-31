#include <gtest/gtest.h>
#include "client/chunker.hpp"
#include "common/sha256.hpp"
#include <fstream>
#include <cstdio>
#include <cstring>

static const char* TMP_FILE    = "/tmp/distfs_test_input.bin";
static const char* TMP_OUT     = "/tmp/distfs_test_reassembled.bin";

class ChunkerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a 12 MB test file of pseudo-random bytes
        std::ofstream f(TMP_FILE, std::ios::binary);
        for (int i = 0; i < 12 * 1024 * 1024; ++i)
            f.put(static_cast<char>(i * 7 + 13)); // deterministic pattern
    }
    void TearDown() override {
        std::remove(TMP_FILE);
        std::remove(TMP_OUT);
    }
};

TEST_F(ChunkerTest, SplitsInto3Chunks) {
    auto chunks = distfs::split_file(TMP_FILE, 4194304);
    EXPECT_EQ(chunks.size(), 3u);
    EXPECT_EQ(chunks[0].chunk_index, 0);
    EXPECT_EQ(chunks[1].chunk_index, 1);
    EXPECT_EQ(chunks[2].chunk_index, 2);
}

TEST_F(ChunkerTest, ChunkHashesAreHex64) {
    auto chunks = distfs::split_file(TMP_FILE, 4194304);
    for (auto& c : chunks) {
        EXPECT_EQ(c.chunk_hash.size(), 64u);
        for (char ch : c.chunk_hash)
            EXPECT_TRUE((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'));
    }
}

TEST_F(ChunkerTest, ReassembleIsIdentical) {
    auto chunks = distfs::split_file(TMP_FILE, 4194304);

    // Write chunk temp files
    std::vector<std::string> tmp_paths;
    std::ifstream src(TMP_FILE, std::ios::binary);
    for (auto& c : chunks) {
        std::string tmp = std::string("/tmp/chunk_") + std::to_string(c.chunk_index) + ".tmp";
        std::ofstream out(tmp, std::ios::binary);
        std::vector<char> buf(c.size_bytes);
        src.read(buf.data(), c.size_bytes);
        out.write(buf.data(), src.gcount());
        tmp_paths.push_back(tmp);
    }

    // Reassemble
    auto out_hash = distfs::reassemble_file(tmp_paths, TMP_OUT);
    auto src_hash = distfs::hash_file(TMP_FILE);
    EXPECT_EQ(out_hash, src_hash);

    // Cleanup temps
    for (auto& t : tmp_paths) std::remove(t.c_str());
}
