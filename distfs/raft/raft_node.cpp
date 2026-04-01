#include "raft/raft_node.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cassert>
#include <cstring>

namespace distfs {

using namespace std::chrono;

// ============================================================
// Constructor / Destructor
// ============================================================

RaftNode::RaftNode(const std::string& node_id,
                   const std::string& wal_path,
                   const std::vector<PeerInfo>& peers,
                   int election_min_ms, int election_max_ms,
                   int heartbeat_ms,
                   StateMachineApply apply_fn)
    : node_id_(node_id), peers_(peers),
      election_min_ms_(election_min_ms), election_max_ms_(election_max_ms),
      heartbeat_ms_(heartbeat_ms),
      rng_(std::random_device{}()),
      log_(wal_path), wal_path_(wal_path),
      apply_fn_(std::move(apply_fn))
{
    load_hard_state();
    last_heartbeat_time_ = steady_clock::now();
    election_timeout_ms_ = random_election_timeout_ms();
}

RaftNode::~RaftNode() { stop(); }

void RaftNode::start() {
    running_ = true;
    election_thread_  = std::thread(&RaftNode::election_timer_loop, this);
    apply_thread_     = std::thread(&RaftNode::apply_loop, this);
    // heartbeat_thread_ started only when becoming LEADER
}

void RaftNode::stop() {
    running_ = false;
    commit_cv_.notify_all();
    apply_cv_.notify_all();
    if (election_thread_.joinable())  election_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    if (apply_thread_.joinable())     apply_thread_.join();
}

// ============================================================
// Hard-state persistence
// ============================================================
// Wire format (appended as a WAL entry):
//   [ current_term: int64_t (8 bytes) ][ voted_for_len: uint32_t (4 bytes) ][ voted_for: bytes ]

std::string RaftNode::hs_path() const { return wal_path_ + ".hs"; }

void RaftNode::persist_hard_state() {
    // Build the entry payload — wire format:
    //   [ current_term: int64_t (8) ][ commit_index: int64_t (8) ]
    //   [ voted_for_len: uint32_t (4) ][ voted_for: bytes ]
    std::vector<uint8_t> buf;
    buf.resize(8 + 8 + 4);
    int64_t  term    = current_term_.load();
    int64_t  ci      = commit_index_.load();
    uint32_t vf_len  = static_cast<uint32_t>(voted_for_.size());
    std::memcpy(buf.data(),      &term,   8);
    std::memcpy(buf.data() + 8,  &ci,     8);
    std::memcpy(buf.data() + 16, &vf_len, 4);
    buf.insert(buf.end(), voted_for_.begin(), voted_for_.end());
    WAL hs(hs_path());
    hs.append(buf);
}

void RaftNode::load_hard_state() {
    // Replay all entries and keep the last one (most recent hard state)
    std::vector<uint8_t> last;
    WAL hs(hs_path());
    hs.replay([&](const std::vector<uint8_t>& data) { last = data; });

    // Support both old format (term+voted_for, 12+ bytes) and
    // new format (term+commit_index+voted_for, 20+ bytes).
    // Distinguish by checking total size: old entries stored 8+4+vf_len bytes,
    // new entries store 8+8+4+vf_len bytes.
    if (last.size() < 12) return; // nothing persisted yet

    int64_t  term       = 0;
    int64_t  saved_ci   = 0;
    uint32_t vf_len     = 0;
    size_t   vf_offset  = 0;

    // Heuristic: if the 4 bytes at offset 8 look like a vf_len that fits
    // in the old 12-byte layout, treat as old format. Otherwise new format.
    uint32_t maybe_old_vf_len = 0;
    std::memcpy(&maybe_old_vf_len, last.data() + 8, 4);
    bool is_new_format = (last.size() >= 20) &&
                         (12 + maybe_old_vf_len != last.size());

    if (is_new_format) {
        // New: term(8) + commit_index(8) + vf_len(4) + vf(vf_len)
        if (last.size() < 20) return;
        std::memcpy(&term,   last.data(),      8);
        std::memcpy(&saved_ci, last.data() + 8, 8);
        std::memcpy(&vf_len, last.data() + 16, 4);
        vf_offset = 20;
    } else {
        // Old: term(8) + vf_len(4) + vf(vf_len)
        std::memcpy(&term,   last.data(),     8);
        std::memcpy(&vf_len, last.data() + 8, 4);
        vf_offset = 12;
        saved_ci = 0; // unknown — will catch up via Raft
    }

    if (last.size() < vf_offset + vf_len) return;
    current_term_.store(term);
    voted_for_.assign(reinterpret_cast<const char*>(last.data() + vf_offset), vf_len);

    // Restore commit_index and let the apply loop catch up immediately
    if (saved_ci > 0) {
        // Clamp to what we actually have in the log
        int64_t log_last = log_.last_index();
        int64_t ci = std::min(saved_ci, log_last);
        commit_index_.store(ci);
    }

    std::cout << "[raft:" << node_id_ << "] Restored hard state: term="
              << term << " commit_index=" << commit_index_.load()
              << " voted_for=" << voted_for_ << "\n";
}

// ============================================================
// Helpers
// ============================================================

int RaftNode::random_election_timeout_ms() {
    std::uniform_int_distribution<int> dist(election_min_ms_, election_max_ms_);
    return dist(rng_);
}

std::string RaftNode::leader_id() const {
    std::lock_guard<std::mutex> lk(mu_);
    return current_leader_id_;
}

int64_t RaftNode::last_log_index() const {
    std::lock_guard<std::mutex> lk(mu_);
    return log_.last_index();
}

bool RaftNode::log_ok(int64_t req_last_index, int64_t req_last_term) const {
    // Caller holds mu_
    int64_t my_last_term  = log_.last_term();
    int64_t my_last_index = log_.last_index();
    return (req_last_term > my_last_term) ||
           (req_last_term == my_last_term && req_last_index >= my_last_index);
}

std::unique_ptr<::distfs::RaftService::Stub> RaftNode::make_stub(const std::string& addr) const {
    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    return ::distfs::RaftService::NewStub(channel);
}

void RaftNode::reset_election_timer() {
    // Caller holds mu_ (or is the only thread accessing this)
    last_heartbeat_time_ = steady_clock::now();
    election_timeout_ms_ = random_election_timeout_ms();
}

// ============================================================
// RequestVote RPC handler (follower / candidate side)
// ============================================================

::distfs::RequestVoteResponse RaftNode::handle_request_vote(
    const ::distfs::RequestVoteRequest& req)
{
    std::lock_guard<std::mutex> lk(mu_);
    ::distfs::RequestVoteResponse resp;
    resp.set_term(current_term_.load());

    if (req.term() < current_term_.load()) {
        resp.set_vote_granted(false);
        return resp;
    }

    // Step down on higher term
    if (req.term() > current_term_.load()) {
        current_term_.store(req.term());
        voted_for_.clear();
        state_.store(RaftState::FOLLOWER);
        current_leader_id_.clear();
        resp.set_term(req.term());
        persist_hard_state();
    }

    bool can_vote = (voted_for_.empty() || voted_for_ == req.candidate_id());
    bool log_up_to_date = log_ok(req.last_log_index(), req.last_log_term());

    if (can_vote && log_up_to_date) {
        voted_for_ = req.candidate_id();
        persist_hard_state();
        reset_election_timer();
        std::cout << "[raft:" << node_id_ << "] Voting for " << req.candidate_id()
                  << " in term " << req.term() << "\n";
        resp.set_vote_granted(true);
    } else {
        resp.set_vote_granted(false);
    }
    return resp;
}

// ============================================================
// AppendEntries RPC handler (follower side)
// ============================================================

::distfs::AppendEntriesResponse RaftNode::handle_append_entries(
    const ::distfs::AppendEntriesRequest& req)
{
    std::lock_guard<std::mutex> lk(mu_);
    ::distfs::AppendEntriesResponse resp;
    resp.set_term(current_term_.load());
    resp.set_success(false);

    if (req.term() < current_term_.load())
        return resp;

    // Valid leader contact
    if (req.term() >= current_term_.load()) {
        bool changed = (req.term() > current_term_.load());
        current_term_.store(req.term());
        if (changed) voted_for_.clear();  // safe — we step down
        state_.store(RaftState::FOLLOWER);
        current_leader_id_ = req.leader_id();
        resp.set_term(req.term());
        if (changed) persist_hard_state();
        reset_election_timer();
    }

    // Consistency check
    int64_t prev_idx  = req.prev_log_index();
    int64_t prev_term = req.prev_log_term();
    if (prev_idx > 0) {
        if (prev_idx > log_.last_index()) return resp; // missing entries
        if (log_.get(prev_idx).term != prev_term) return resp; // conflict
    }

    // Append / reconcile entries
    for (int i = 0; i < req.entries_size(); ++i) {
        int64_t idx = prev_idx + 1 + i;
        const auto& e = req.entries(i);
        if (idx <= log_.last_index()) {
            if (log_.get(idx).term != e.term()) {
                log_.truncate(idx); // conflict — truncate from here
            } else {
                continue; // already have this entry
            }
        }
        RaftLogEntry le;
        le.term = e.term();
        le.command.assign(e.command().begin(), e.command().end());
        log_.append(le);
    }

    // Advance commit index
    if (req.leader_commit() > commit_index_.load()) {
        int64_t new_commit = std::min(req.leader_commit(), log_.last_index());
        commit_index_.store(new_commit);
        persist_hard_state(); // survive restarts
        apply_cv_.notify_one();
    }

    resp.set_success(true);
    return resp;
}

// ============================================================
// Election Timer Loop
// ============================================================

void RaftNode::election_timer_loop() {
    while (running_) {
        std::this_thread::sleep_for(milliseconds(10));
        std::unique_lock<std::mutex> lk(mu_);
        if (state_.load() == RaftState::LEADER) continue;
        auto elapsed = duration_cast<milliseconds>(
            steady_clock::now() - last_heartbeat_time_).count();
        if (elapsed >= election_timeout_ms_) {
            lk.unlock();
            start_election();
        }
    }
}

void RaftNode::start_election() {
    int64_t new_term;
    {
        std::lock_guard<std::mutex> lk(mu_);
        current_term_.fetch_add(1);
        new_term = current_term_.load();
        voted_for_ = node_id_;
        state_.store(RaftState::CANDIDATE);
        current_leader_id_.clear();
        persist_hard_state();
        reset_election_timer();
        std::cout << "[raft:" << node_id_ << "] Starting election for term " << new_term << "\n";
    }

    int cluster_size = static_cast<int>(peers_.size()) + 1;
    int majority      = cluster_size / 2 + 1;
    std::atomic<int> votes{1}; // self-vote
    std::mutex vote_mu;

    std::vector<std::thread> threads;
    for (const auto& peer : peers_) {
        threads.emplace_back([&, peer]() {
            ::distfs::RequestVoteRequest req;
            {
                std::lock_guard<std::mutex> lk(mu_);
                req.set_term(new_term);
                req.set_candidate_id(node_id_);
                req.set_last_log_index(log_.last_index());
                req.set_last_log_term(log_.last_term());
            }

            ::distfs::RequestVoteResponse resp;
            grpc::ClientContext ctx;
            ctx.set_deadline(system_clock::now() + milliseconds(200));
            auto stub = make_stub(peer.address);
            auto status = stub->RequestVote(&ctx, req, &resp);
            if (!status.ok()) return;

            {
                std::lock_guard<std::mutex> lk(mu_);
                if (resp.term() > current_term_.load()) {
                    current_term_.store(resp.term());
                    voted_for_.clear();
                    persist_hard_state();
                    state_.store(RaftState::FOLLOWER);
                    return;
                }
            }

            if (resp.vote_granted()) {
                int v = votes.fetch_add(1) + 1;
                if (v >= majority) {
                    // Become leader (idempotent — guarded by CAS-like state check)
                    std::lock_guard<std::mutex> lk(mu_);
                    if (state_.load() != RaftState::CANDIDATE) return;
                    state_.store(RaftState::LEADER);
                    current_leader_id_ = node_id_;
                    // Initialize leader state
                    for (const auto& p : peers_) {
                        next_index_[p.id]  = log_.last_index() + 1;
                        match_index_[p.id] = 0;
                    }
                    std::cout << "[raft:" << node_id_ << "] Became leader term "
                              << new_term << "\n";
                    // Start heartbeat thread
                    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
                    heartbeat_thread_ = std::thread(&RaftNode::heartbeat_loop, this);
                }
            }
        });
    }
    for (auto& t : threads) t.join();
}

// ============================================================
// Heartbeat / Log Replication (leader side)
// ============================================================

void RaftNode::heartbeat_loop() {
    while (running_ && state_.load() == RaftState::LEADER) {
        for (const auto& peer : peers_)
            send_append_entries(peer);
        std::this_thread::sleep_for(milliseconds(heartbeat_ms_));
    }
}

void RaftNode::send_append_entries(const PeerInfo& peer) {
    ::distfs::AppendEntriesRequest req;
    std::vector<uint8_t> cmd_buf; // local copies to avoid lock during RPC

    {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_.load() != RaftState::LEADER) return;

        req.set_term(current_term_.load());
        req.set_leader_id(node_id_);
        req.set_leader_commit(commit_index_.load());

        int64_t ni       = next_index_.count(peer.id) ? next_index_[peer.id] : 1;
        int64_t prev_idx = ni - 1;
        int64_t prev_term = 0;
        if (prev_idx > 0 && prev_idx <= log_.last_index())
            prev_term = log_.get(prev_idx).term;

        req.set_prev_log_index(prev_idx);
        req.set_prev_log_term(prev_term);

        for (int64_t i = ni; i <= log_.last_index(); ++i) {
            auto e = log_.get(i);
            auto* pe = req.add_entries();
            pe->set_term(e.term);
            pe->set_command(e.command.data(), e.command.size());
        }
    }

    ::distfs::AppendEntriesResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(system_clock::now() + milliseconds(heartbeat_ms_ * 2));
    auto stub = make_stub(peer.address);
    auto status = stub->AppendEntries(&ctx, req, &resp);
    if (!status.ok()) return;

    std::lock_guard<std::mutex> lk(mu_);
    if (resp.term() > current_term_.load()) {
        current_term_.store(resp.term());
        voted_for_.clear();
        persist_hard_state();
        state_.store(RaftState::FOLLOWER);
        current_leader_id_.clear();
        return;
    }

    if (resp.success()) {
        int64_t new_match = req.prev_log_index() + req.entries_size();
        match_index_[peer.id] = std::max(match_index_[peer.id], new_match);
        next_index_[peer.id]  = match_index_[peer.id] + 1;
        maybe_advance_commit_index();
    } else {
        // Backtrack
        if (next_index_.count(peer.id) && next_index_[peer.id] > 1)
            next_index_[peer.id]--;
    }
}

void RaftNode::maybe_advance_commit_index() {
    // Caller holds mu_
    int64_t last = log_.last_index();
    int majority = (static_cast<int>(peers_.size()) + 1) / 2 + 1;
    for (int64_t n = last; n > commit_index_.load(); --n) {
        if (log_.get(n).term != current_term_.load()) continue;
        int count = 1; // self
        for (const auto& p : peers_)
            if (match_index_.count(p.id) && match_index_.at(p.id) >= n) ++count;
        if (count >= majority) {
            commit_index_.store(n);
            persist_hard_state(); // survive restarts
            apply_cv_.notify_one();
            commit_cv_.notify_all();
            break;
        }
    }
}

// ============================================================
// Apply Loop (background thread)
// ============================================================

void RaftNode::apply_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lk(mu_);
        apply_cv_.wait(lk, [&]{
            return !running_ || last_applied_.load() < commit_index_.load();
        });
        while (last_applied_.load() < commit_index_.load()) {
            int64_t next = last_applied_.load() + 1;
            auto entry = log_.get(next);
            lk.unlock();
            if (apply_fn_) apply_fn_(entry.command);
            lk.lock();
            last_applied_.store(next);
        }
    }
}

// ============================================================
// ClientRequest (called by metadata server gRPC handlers)
// ============================================================

bool RaftNode::submit(const std::vector<uint8_t>& command) {
    int64_t my_index;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_.load() != RaftState::LEADER) return false;
        RaftLogEntry e;
        e.term    = current_term_.load();
        e.command = command;
        my_index  = log_.append(e);
        // Immediately try to replicate
        for (const auto& peer : peers_)
            next_index_[peer.id]; // ensure entry exists (default 0 is fine, send_ae checks)
    }

    // Trigger one round of AppendEntries on all peers
    for (const auto& peer : peers_) {
        std::thread([this, peer](){ send_append_entries(peer); }).detach();
    }

    // Block until commit_index >= my_index (or we lose leadership)
    std::unique_lock<std::mutex> lk(mu_);
    commit_cv_.wait_for(lk, std::chrono::seconds(5), [&]{
        return commit_index_.load() >= my_index ||
               state_.load() != RaftState::LEADER;
    });
    return commit_index_.load() >= my_index;
}

} // namespace distfs
