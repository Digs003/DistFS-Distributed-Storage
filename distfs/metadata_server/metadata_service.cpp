#include "metadata_server/metadata_service.hpp"
#include "common/logger.hpp"
#include <chrono>
#include <ctime>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <sstream>
#include <thread>

namespace distfs {

MetadataServiceImpl::MetadataServiceImpl(MetadataStore &store, RaftNode &raft,
                                         int dead_threshold_sec,
                                         int replication_factor)
    : store_(store), raft_(raft), dead_threshold_sec_(dead_threshold_sec),
      replication_factor_(replication_factor) {}

MetadataServiceImpl::~MetadataServiceImpl() { stop_monitors(); }

void MetadataServiceImpl::start_monitors() {
  running_ = true;
  heartbeat_monitor_thread_ =
      std::thread(&MetadataServiceImpl::heartbeat_monitor_loop, this);
}
void MetadataServiceImpl::stop_monitors() {
  running_ = false;
  if (heartbeat_monitor_thread_.joinable())
    heartbeat_monitor_thread_.join();
}

grpc::Status MetadataServiceImpl::require_leader() const {
  if (!raft_.is_leader()) {
    std::string hint = raft_.leader_id();
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "Not leader. Leader hint: " + hint);
  }
  return grpc::Status::OK;
}

// ---- InitiateUpload ----
grpc::Status
MetadataServiceImpl::InitiateUpload(grpc::ServerContext *,
                                    const ::distfs::InitiateUploadRequest *req,
                                    ::distfs::InitiateUploadResponse *resp) {
  VLOG("metadata", "InitiateUpload: file='" + req->filename() +
       "' chunks=" + std::to_string(req->chunks_size()));
  auto s = require_leader(); 
  if (!s.ok()) {
    VLOG("metadata", "InitiateUpload rejected: not leader");
    return s;
  }
  if (store_.file_exists(req->filename())) {
    VLOG("metadata", "InitiateUpload rejected: file already exists '" + req->filename() + "'");
    return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                        "File already exists: " + req->filename());
  }

  auto alive = store_.alive_nodes();
  if (static_cast<int>(alive.size()) < replication_factor_) {
    VLOG("metadata", "InitiateUpload rejected: only " +
         std::to_string(alive.size()) + " alive node(s) but RF=" + std::to_string(replication_factor_));
    return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                        "Need at least " + std::to_string(replication_factor_) + " alive storage nodes");
  }

  std::string upload_token =
      req->filename() + "_" + std::to_string(std::time(nullptr));
  resp->set_upload_token(upload_token);

  std::vector<LocalNodePlacement> transient_placements;

  for (int i = 0; i < req->chunks_size(); ++i) {
    const auto &chunk = req->chunks(i);
    std::string primary = store_.select_primary();
    std::string secondary;
    if (replication_factor_ > 1) {
        secondary = store_.select_secondary(primary);
    }
    auto *p = resp->add_placements();
    p->set_chunk_hash(chunk.chunk_hash());
    p->set_primary_node(primary);
    if (!secondary.empty()) {
        p->set_secondary_node(secondary);
    }

    LocalNodePlacement loc;
    loc.chunk_hash = chunk.chunk_hash();
    loc.primary_node = primary;
    loc.secondary_node = secondary;

    // Resolve routable addresses from registry
    std::string paddr = store_.node_address(primary);
    if (!paddr.empty()) {
      p->set_primary_addr(paddr);
      loc.primary_addr = paddr;
    }
    if (!secondary.empty()) {
        std::string saddr = store_.node_address(secondary);
        if (!saddr.empty()) {
            p->set_secondary_addr(saddr);
            loc.secondary_addr = saddr;
        }
    }
    VLOG("metadata", "  chunk[" + std::to_string(i) + "] " +
         chunk.chunk_hash().substr(0, 8) + "... -> primary=" + primary +
         " secondary=" + (secondary.empty() ? "(none)" : secondary));
    transient_placements.push_back(loc);
  }

  {
    std::lock_guard<std::mutex> lock(pending_mu_);
    pending_placements_[upload_token] = transient_placements;
  }

  VLOG("metadata", "InitiateUpload OK: token=" + upload_token);
  return grpc::Status::OK;
}

// ---- CommitUpload ----
grpc::Status
MetadataServiceImpl::CommitUpload(grpc::ServerContext *,
                                  const ::distfs::CommitUploadRequest *req,
                                  ::distfs::CommitUploadResponse *resp) {
  VLOG("metadata", "CommitUpload: file='" + req->filename() +
       "' token=" + req->upload_token());
  auto s = require_leader(); 
  if (!s.ok()) {
    VLOG("metadata", "CommitUpload rejected: not leader");
    return s;
  }

  std::vector<std::string> hashes;
  std::vector<LocalNodePlacement> placements;
  {
    std::lock_guard<std::mutex> lock(pending_mu_);
    auto it = pending_placements_.find(req->upload_token());
    if (it != pending_placements_.end()) {
      placements = it->second;
      pending_placements_.erase(it);
    } else {
      VLOG("metadata", "CommitUpload failed: expired/invalid token '" + req->upload_token() + "'");
      resp->set_success(false);
      resp->set_error("Expired or invalid upload token");
      return grpc::Status::OK;
    }
  }

  int64_t total_bytes = 0;
  for (auto &c : req->chunks()) {
    hashes.push_back(c.chunk_hash());
    total_bytes += c.size_bytes();
  }

  int64_t rev = raft_.current_term() * 1000 + raft_.last_log_index();
  auto cmd = MetadataStore::cmd_commit_upload(req->filename(), rev, total_bytes,
                                              hashes, placements);

  if (!raft_.submit(cmd)) {
    VLOG("metadata", "CommitUpload failed: Raft submit rejected (lost leadership)");
    resp->set_success(false);
    resp->set_error("Raft commit failed — lost leadership");
    return grpc::Status::OK;
  }
  VLOG("metadata", "CommitUpload OK: file='" + req->filename() +
       "' rev=" + std::to_string(rev) +
       " chunks=" + std::to_string(hashes.size()) +
       " bytes=" + std::to_string(total_bytes));
  resp->set_success(true);
  resp->set_revision_id(rev);
  return grpc::Status::OK;
}

// ---- GetFileMetadata ----
grpc::Status MetadataServiceImpl::GetFileMetadata(
    grpc::ServerContext *, const ::distfs::GetFileMetadataRequest *req,
    ::distfs::GetFileMetadataResponse *resp) {
  VLOG("metadata", "GetFileMetadata: file='" + req->filename() + "'");
  try {
    auto rec = store_.get_file(req->filename());
    resp->set_found(true);
    auto *m = resp->mutable_metadata();
    m->set_filename(rec.filename);
    m->set_revision_id(rec.revision_id);
    m->set_total_size_bytes(rec.total_size_bytes);
    for (size_t i = 0; i < rec.chunk_hashes.size(); ++i) {
      auto *ci = m->add_chunks();
      ci->set_chunk_hash(rec.chunk_hashes[i]);
      ci->set_chunk_index(static_cast<int32_t>(i));
      auto nodes = store_.get_chunk_nodes(rec.chunk_hashes[i]);
      auto *pl = m->add_placements();
      pl->set_chunk_hash(rec.chunk_hashes[i]);
      if (!nodes.empty()) {
        pl->set_primary_node(nodes[0]);
        std::string paddr = store_.node_address(nodes[0]);
        if (!paddr.empty())
          pl->set_primary_addr(paddr);
      }
      if (nodes.size() > 1) {
        pl->set_secondary_node(nodes[1]);
        std::string saddr = store_.node_address(nodes[1]);
        if (!saddr.empty())
          pl->set_secondary_addr(saddr);
      }
    }
    VLOG("metadata", "GetFileMetadata OK: file='" + req->filename() +
         "' chunks=" + std::to_string(rec.chunk_hashes.size()) +
         " bytes=" + std::to_string(rec.total_size_bytes));
  } catch (...) {
    VLOG("metadata", "GetFileMetadata: file='" + req->filename() + "' not found");
    resp->set_found(false);
  }
  return grpc::Status::OK;
}

// ---- ListFiles ----
grpc::Status MetadataServiceImpl::ListFiles(grpc::ServerContext *,
                                            const ::distfs::ListFilesRequest *,
                                            ::distfs::ListFilesResponse *resp) {
  auto files = store_.list_files();
  VLOG("metadata", "ListFiles: returning " + std::to_string(files.size()) + " file(s)");
  for (auto &rec : files) {
    auto *fe = resp->add_files();
    fe->set_filename(rec.filename);
    fe->set_chunk_count(static_cast<int32_t>(rec.chunk_hashes.size()));
    fe->set_total_bytes(rec.total_size_bytes);
    fe->set_revision_id(rec.revision_id);
  }
  return grpc::Status::OK;
}

// ---- DeleteFile ----
grpc::Status
MetadataServiceImpl::DeleteFile(grpc::ServerContext *,
                                const ::distfs::DeleteFileRequest *req,
                                ::distfs::DeleteFileResponse *resp) {
  VLOG("metadata", "DeleteFile: file='" + req->filename() + "'");
  auto s = require_leader(); 
  if (!s.ok()) {
    VLOG("metadata", "DeleteFile rejected: not leader");
    return s;
  }
  auto cmd = MetadataStore::cmd_delete_file(req->filename());
  if (!raft_.submit(cmd)) {
    VLOG("metadata", "DeleteFile failed: Raft submit rejected");
    resp->set_success(false);
    resp->set_error("Raft commit failed");
    return grpc::Status::OK;
  }
  VLOG("metadata", "DeleteFile OK: file='" + req->filename() + "'");
  resp->set_success(true);
  return grpc::Status::OK;
}

// ---- RegisterHeartbeat ----
grpc::Status
MetadataServiceImpl::RegisterHeartbeat(grpc::ServerContext *,
                                       const ::distfs::HeartbeatRequest *req,
                                       ::distfs::HeartbeatResponse *resp) {
  if (!raft_.is_leader()) {
    resp->set_ok(false);
    resp->set_leader_hint(raft_.leader_id());
    return grpc::Status::OK;
  }
  VLOG("metadata", "Heartbeat from node='" + req->node_id() +
       "' addr=" + req->address() +
       " used=" + std::to_string(req->used_bytes()) +
       " total=" + std::to_string(req->total_bytes()) +
       " chunks=" + std::to_string(req->chunk_count()));
  store_.register_heartbeat(*req);
  resp->set_ok(true);
  return grpc::Status::OK;
}

// ---- GetClusterStatus ----
grpc::Status
MetadataServiceImpl::GetClusterStatus(grpc::ServerContext *,
                                      const ::distfs::StatusRequest *,
                                      ::distfs::StatusResponse *resp) {
  VLOG("metadata", "GetClusterStatus: leader='" + raft_.leader_id() +
       "' term=" + std::to_string(raft_.current_term()) +
       " logIdx=" + std::to_string(raft_.last_log_index()));
  resp->set_leader_id(raft_.leader_id());
  resp->set_current_term(raft_.current_term());
  resp->set_log_index(raft_.last_log_index());
  resp->set_total_chunks(store_.total_chunks());
  resp->set_under_replicated(store_.under_replicated(replication_factor_));
  resp->set_orphaned_chunks(store_.orphaned_chunks());
  for (auto &n : store_.all_nodes()) {
    auto *ns = resp->add_storage_nodes();
    ns->set_address(n.address);
    ns->set_is_alive(n.status == NodeInfo::Status::ALIVE);
    ns->set_used_bytes(n.used_bytes);
    ns->set_total_bytes(n.total_bytes);
    ns->set_chunk_count(n.chunk_count);
  }
  return grpc::Status::OK;
}

// ---- Heartbeat Monitor (runs on leader only) ----
void MetadataServiceImpl::heartbeat_monitor_loop() {
  while (running_) {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    if (!raft_.is_leader())
      continue;
    int64_t now = std::time(nullptr);
    for (auto &n : store_.all_nodes()) {
      if (n.status == NodeInfo::Status::DEAD)
        continue;
      if (now - n.last_heartbeat > dead_threshold_sec_) {
        std::cout << "[monitor] Node " << n.address << " (" << n.node_id
                  << ") missed heartbeats — marking DEAD\n";
        // Mark dead locally so subsequent calls don't re-trigger
        store_.mark_dead(n.node_id);
        // Kick off re-replication on a detached thread so we don't block the
        // monitor
        std::string dead_id = n.node_id;
        std::thread([this, dead_id]() {
          trigger_re_replication(dead_id);
        }).detach();
      }
    }
  }
}

void MetadataServiceImpl::trigger_re_replication(const NodeID &dead_node) {
  std::cout << "[monitor] TriggerReReplication for " << dead_node << "\n";

  // For each chunk where dead_node is listed, pick a surviving node to
  // replicate from and a new live target to replicate to.
  for (auto &n : store_.all_nodes()) {
    if (n.node_id == dead_node)
      continue; // not the dead node
    // We iterate all chunks via the store's chunk stats indirectly —
    // the full scan needs chunk_map access; query via get_chunk_nodes
    // is chunk-hash driven. We drive from the file map instead.
    (void)n; // actual per-chunk fan-out below
  }

  // Scan: for every file's chunk, check if dead_node is in its node list
  auto files = store_.list_files();
  for (auto &rec : files) {
    for (auto &hash : rec.chunk_hashes) {
      auto nodes = store_.get_chunk_nodes(hash);
      bool dead_is_here = false;
      for (auto &nid : nodes)
        if (nid == dead_node) {
          dead_is_here = true;
          break;
        }
      if (!dead_is_here)
        continue;

      // Find a survivor to read from
      std::string survivor;
      for (auto &nid : nodes)
        if (nid != dead_node) {
          survivor = nid;
          break;
        }
      if (survivor.empty()) {
        std::cerr << "[monitor] CRITICAL: no surviving replica for chunk "
                  << hash << " — data loss!\n";
        continue;
      }

      // Find an alive target not already holding the chunk
      std::string target;
      for (auto &ni : store_.alive_nodes()) {
        bool already_has = false;
        for (auto &nid : nodes)
          if (nid == ni.node_id) {
            already_has = true;
            break;
          }
        if (!already_has) {
          target = ni.node_id;
          break;
        }
      }
      if (target.empty()) {
        std::cout << "[monitor] No spare node for chunk " << hash << "\n";
        continue;
      }

      // Send ReplicateChunk RPC to the surviving node
      std::string survivor_addr = store_.node_address(survivor);
      std::string target_addr = store_.node_address(target);
      if (survivor_addr.empty() || target_addr.empty())
        continue;

      auto chan = grpc::CreateChannel(survivor_addr,
                                      grpc::InsecureChannelCredentials());
      auto stub = ::distfs::StorageService::NewStub(chan);
      ::distfs::ReplicateRequest rreq;
      rreq.set_chunk_hash(hash);
      rreq.set_target_addr(target_addr);
      ::distfs::ChunkAck ack;
      grpc::ClientContext ctx;
      ctx.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::seconds(30));
      auto st = stub->ReplicateChunk(&ctx, rreq, &ack);
      if (st.ok() && ack.success()) {
        std::cout << "[monitor] Re-replicated chunk " << hash.substr(0, 8)
                  << "... to " << target << "\n";
        // Update chunk map: remove dead_node, add target
        std::vector<NodeID> new_nodes;
        for (auto &nid : nodes)
          if (nid != dead_node)
            new_nodes.push_back(nid);
        new_nodes.push_back(target);
        auto cmd = MetadataStore::cmd_update_chunk_map(hash, new_nodes);
        raft_.submit(cmd);
      } else {
        std::cerr << "[monitor] ReplicateChunk failed for " << hash.substr(0, 8)
                  << ": " << ack.error_message() << "\n";
      }
    }
  }
  std::cout << "[monitor] Re-replication scan complete for " << dead_node
            << "\n";
}

} // namespace distfs
