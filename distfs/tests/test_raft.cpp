#include <gtest/gtest.h>
#include "raft/raft_log.hpp"
#include <cstdio>
#include <string>

static const char* LOG_WAL = "/tmp/distfs_test_raftlog.bin";

class RaftLogTest : public ::testing::Test {
protected:
    void SetUp()    override { std::remove(LOG_WAL); }
    void TearDown() override { std::remove(LOG_WAL); }
};

TEST_F(RaftLogTest, AppendAndGet) {
    distfs::RaftLog log(LOG_WAL);
    distfs::RaftLogEntry e;
    e.term = 1;
    e.command = {0x01, 0x02, 0x03};
    int64_t idx = log.append(e);
    EXPECT_EQ(idx, 1);
    auto got = log.get(1);
    EXPECT_EQ(got.term, 1);
    EXPECT_EQ(got.command, e.command);
}

TEST_F(RaftLogTest, Replay100Entries) {
    {
        distfs::RaftLog log(LOG_WAL);
        for (int i = 0; i < 100; ++i) {
            distfs::RaftLogEntry e;
            e.term = i / 10 + 1;
            std::string cmd = "cmd_" + std::to_string(i);
            e.command.assign(cmd.begin(), cmd.end());
            log.append(e);
        }
        EXPECT_EQ(log.last_index(), 100);
    }
    // Re-open: replay from WAL
    distfs::RaftLog log2(LOG_WAL);
    EXPECT_EQ(log2.last_index(), 100);
    for (int i = 0; i < 100; ++i) {
        auto e = log2.get(i + 1);
        std::string cmd(e.command.begin(), e.command.end());
        EXPECT_EQ(cmd, "cmd_" + std::to_string(i));
        EXPECT_EQ(e.term, i / 10 + 1);
    }
}

TEST_F(RaftLogTest, TruncateAndReplay) {
    distfs::RaftLog log(LOG_WAL);
    for (int i = 0; i < 10; ++i) {
        distfs::RaftLogEntry e; e.term = 1;
        std::string c = "e" + std::to_string(i);
        e.command.assign(c.begin(), c.end());
        log.append(e);
    }
    log.truncate(6); // remove entries 6..10
    EXPECT_EQ(log.last_index(), 5);

    // Reload and confirm truncation persisted
    distfs::RaftLog log2(LOG_WAL);
    EXPECT_EQ(log2.last_index(), 5);
    auto e5 = log2.get(5);
    std::string s(e5.command.begin(), e5.command.end());
    EXPECT_EQ(s, "e4");
}
