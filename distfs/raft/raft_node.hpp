#pragma once
#include "raft/raft_log.hpp"
#include "proto_gen/distfs.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <random>

namespace distfs {

enum class RaftState { FOLLOWER, CANDIDATE, LEADER };

struct PeerInfo {
    std::string id;
    std::string address;
};

/// Full Raft consensus node.
/// Thread-safe. Runs election timer, heartbeat sender, and RPC handlers.
class RaftNode {
public:
    using StateMachineApply = std::function<void(const std::vector<uint8_t>& command)>;

    RaftNode(const std::string& node_id,
             const std::string& wal_path,
             const std::vector<PeerInfo>& peers,
             int election_timeout_min_ms,
             int election_timeout_max_ms,
             int heartbeat_interval_ms,
             StateMachineApply apply_fn);

    ~RaftNode();

    void start();
    void stop();

    // ---- gRPC RPC handlers (called by RaftServiceImpl) ----
    ::distfs::RequestVoteResponse   handle_request_vote(const ::distfs::RequestVoteRequest& req);
    ::distfs::AppendEntriesResponse handle_append_entries(const ::distfs::AppendEntriesRequest& req);

    /// Submit a command as a Raft ClientRequest (leader only).
    /// Blocks until committed or returns false if not leader.
    bool submit(const std::vector<uint8_t>& command);

    // ---- State inspection ----
    bool        is_leader()       const { return state_.load() == RaftState::LEADER; }
    std::string leader_id()       const;
    int64_t     current_term()    const { return current_term_.load(); }
    int64_t     commit_index()    const { return commit_index_.load(); }
    int64_t     last_log_index()  const;
    std::string node_id()         const { return node_id_; }

private:
    // ---- Persistent state (must be saved before responding to RPCs) ----
    void persist_hard_state();
    void load_hard_state();
    std::string hs_path() const;

    // ---- Election ----
    void election_timer_loop();
    void reset_election_timer();
    void start_election();

    // ---- Heartbeat / log replication ----
    void heartbeat_loop();
    void send_append_entries(const PeerInfo& peer);

    // ---- Commit ----
    void maybe_advance_commit_index();
    void apply_loop();

    // ---- Helpers ----
    int random_election_timeout_ms();
    bool log_ok(int64_t last_log_index, int64_t last_log_term) const;
    std::unique_ptr<::distfs::RaftService::Stub> make_stub(const std::string& addr) const;

    // ---- Identity ----
    const std::string        node_id_;
    const std::vector<PeerInfo> peers_;

    // ---- Timing ----
    const int election_min_ms_;
    const int election_max_ms_;
    const int heartbeat_ms_;
    std::mt19937 rng_;

    // ---- Persistent state (protected by mu_) ----
    std::atomic<int64_t> current_term_{0};
    std::string          voted_for_;  // "" if none
    RaftLog              log_;
    std::string          wal_path_;

    // ---- Volatile state ----
    std::atomic<RaftState>  state_{RaftState::FOLLOWER};
    std::atomic<int64_t>    commit_index_{0};
    std::atomic<int64_t>    last_applied_{0};
    std::string             current_leader_id_;

    // ---- Leader volatile state ----
    std::map<std::string, int64_t> next_index_;   // peer_id → next index to send
    std::map<std::string, int64_t> match_index_;  // peer_id → highest matched

    // ---- State machine callback ----
    StateMachineApply apply_fn_;

    // ---- Synchronization ----
    mutable std::mutex mu_;
    std::condition_variable commit_cv_;  // signals when commit_index advances
    std::condition_variable apply_cv_;   // signals apply_loop

    // ---- Election timer ----
    std::atomic<bool>    running_{false};
    std::chrono::steady_clock::time_point last_heartbeat_time_;
    int                  election_timeout_ms_ = 150;

    // ---- Threads ----
    std::thread election_thread_;
    std::thread heartbeat_thread_;
    std::thread apply_thread_;
};

/// gRPC service adapter — delegates to RaftNode
class RaftServiceImpl final : public ::distfs::RaftService::Service {
public:
    explicit RaftServiceImpl(RaftNode& node) : node_(node) {}

    grpc::Status RequestVote(grpc::ServerContext*,
                              const ::distfs::RequestVoteRequest* req,
                              ::distfs::RequestVoteResponse* resp) override {
        *resp = node_.handle_request_vote(*req);
        return grpc::Status::OK;
    }
    grpc::Status AppendEntries(grpc::ServerContext*,
                                const ::distfs::AppendEntriesRequest* req,
                                ::distfs::AppendEntriesResponse* resp) override {
        *resp = node_.handle_append_entries(*req);
        return grpc::Status::OK;
    }
private:
    RaftNode& node_;
};

} // namespace distfs
