#include "storage_daemon/heartbeat_client.hpp"
#include "proto_gen/distfs.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <chrono>

namespace distfs {

HeartbeatClient::HeartbeatClient(const std::string& node_id,
                                  const std::string& self_addr,
                                  const std::vector<std::string>& metadata_addrs,
                                  int heartbeat_interval_sec)
    : node_id_(node_id), self_addr_(self_addr),
      metadata_addrs_(metadata_addrs), interval_sec_(heartbeat_interval_sec) {}

HeartbeatClient::~HeartbeatClient() { stop(); }

void HeartbeatClient::start() {
    running_ = true;
    thread_ = std::thread(&HeartbeatClient::run, this);
}

void HeartbeatClient::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void HeartbeatClient::update_stats(int64_t used_bytes, int64_t total_bytes, int64_t chunk_count) {
    std::lock_guard<std::mutex> lock(stats_mu_);
    used_bytes_  = used_bytes;
    total_bytes_ = total_bytes;
    chunk_count_ = chunk_count;
}

void HeartbeatClient::run() {
    while (running_) {
        size_t idx = current_leader_idx_.load();
        bool sent = send_heartbeat(metadata_addrs_[idx]);
        if (!sent) {
            // Try next node in round-robin
            size_t next = (idx + 1) % metadata_addrs_.size();
            current_leader_idx_.store(next);
        }
        std::this_thread::sleep_for(std::chrono::seconds(interval_sec_));
    }
}

bool HeartbeatClient::send_heartbeat(const std::string& addr) {
    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    auto stub = ::distfs::MetadataService::NewStub(channel);

    ::distfs::HeartbeatRequest req;
    req.set_node_id(node_id_);
    req.set_address(self_addr_);
    {
        std::lock_guard<std::mutex> lock(stats_mu_);
        req.set_used_bytes(used_bytes_);
        req.set_total_bytes(total_bytes_);
        req.set_chunk_count(chunk_count_);
    }

    ::distfs::HeartbeatResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));

    auto status = stub->RegisterHeartbeat(&ctx, req, &resp);
    if (!status.ok()) {
        std::cerr << "[heartbeat] Failed to reach " << addr << ": "
                  << status.error_message() << "\n";
        return false;
    }

    if (!resp.leader_hint().empty() && resp.leader_hint() != addr) {
        // Metadata server redirected us to the actual leader
        for (size_t i = 0; i < metadata_addrs_.size(); ++i) {
            if (metadata_addrs_[i] == resp.leader_hint()) {
                current_leader_idx_.store(i);
                break;
            }
        }
    }
    return true;
}

} // namespace distfs
