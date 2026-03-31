#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

namespace distfs {

/// Background thread that sends RegisterHeartbeat RPCs to the
/// metadata server every heartbeat_interval_sec seconds.
class HeartbeatClient {
public:
    HeartbeatClient(const std::string& node_id,
                    const std::string& self_addr,
                    const std::vector<std::string>& metadata_addrs,
                    int heartbeat_interval_sec = 3);

    ~HeartbeatClient();

    void start();
    void stop();

    // Called by the storage daemon to update capacity stats
    void update_stats(int64_t used_bytes, int64_t total_bytes, int64_t chunk_count);

private:
    void run();
    bool send_heartbeat(const std::string& addr);

    std::string              node_id_;
    std::string              self_addr_;
    std::vector<std::string> metadata_addrs_;
    int                      interval_sec_;

    std::atomic<bool>    running_{false};
    std::thread          thread_;
    std::mutex           stats_mu_;
    int64_t              used_bytes_   = 0;
    int64_t              total_bytes_  = 0;
    int64_t              chunk_count_  = 0;

    // Index into metadata_addrs_ of current known leader
    std::atomic<size_t> current_leader_idx_{0};
};

} // namespace distfs
