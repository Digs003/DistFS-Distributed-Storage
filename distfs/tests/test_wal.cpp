#include <gtest/gtest.h>
#include "common/wal.hpp"
#include <cstdio>
#include <string>

static const char* WAL_PATH = "/tmp/distfs_test_wal.bin";

class WALTest : public ::testing::Test {
protected:
    void SetUp()    override { std::remove(WAL_PATH); }
    void TearDown() override { std::remove(WAL_PATH); }
};

TEST_F(WALTest, WriteAndReplay100Entries) {
    {
        distfs::WAL wal(WAL_PATH);
        for (int i = 0; i < 100; ++i) {
            std::string data = "entry_" + std::to_string(i);
            wal.append(data);
        }
    }

    // Re-open and replay
    distfs::WAL wal2(WAL_PATH);
    int count = 0;
    wal2.replay([&](const std::vector<uint8_t>& data) {
        std::string s(data.begin(), data.end());
        EXPECT_EQ(s, "entry_" + std::to_string(count));
        ++count;
    });
    EXPECT_EQ(count, 100);
}

TEST_F(WALTest, EntryCountMatches) {
    distfs::WAL wal(WAL_PATH);
    for (int i = 0; i < 42; ++i)
        wal.append(std::string("x"));
    EXPECT_EQ(wal.entry_count(), 42u);
}

TEST_F(WALTest, EmptyReplay) {
    distfs::WAL wal(WAL_PATH);
    int count = 0;
    wal.replay([&](const std::vector<uint8_t>&){ ++count; });
    EXPECT_EQ(count, 0);
}
