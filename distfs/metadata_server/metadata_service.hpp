#pragma once
#include "metadata_server/metadata_store.hpp"
#include "proto_gen/distfs.grpc.pb.h"
#include "raft/raft_node.hpp"
#include <atomic>
#include <grpcpp/grpcpp.h>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace distfs {

class MetadataServiceImpl final : public ::distfs::MetadataService::Service {
public:
  MetadataServiceImpl(MetadataStore &store, RaftNode &raft,
                      int dead_threshold_sec = 9, int replication_factor = 2);
  ~MetadataServiceImpl();

  void start_monitors();
  void stop_monitors();

  grpc::Status InitiateUpload(grpc::ServerContext *,
                              const ::distfs::InitiateUploadRequest *,
                              ::distfs::InitiateUploadResponse *) override;
  grpc::Status CommitUpload(grpc::ServerContext *,
                            const ::distfs::CommitUploadRequest *,
                            ::distfs::CommitUploadResponse *) override;
  grpc::Status GetFileMetadata(grpc::ServerContext *,
                               const ::distfs::GetFileMetadataRequest *,
                               ::distfs::GetFileMetadataResponse *) override;
  grpc::Status ListFiles(grpc::ServerContext *,
                         const ::distfs::ListFilesRequest *,
                         ::distfs::ListFilesResponse *) override;
  grpc::Status DeleteFile(grpc::ServerContext *,
                          const ::distfs::DeleteFileRequest *,
                          ::distfs::DeleteFileResponse *) override;
  grpc::Status RegisterHeartbeat(grpc::ServerContext *,
                                 const ::distfs::HeartbeatRequest *,
                                 ::distfs::HeartbeatResponse *) override;
  grpc::Status GetClusterStatus(grpc::ServerContext *,
                                const ::distfs::StatusRequest *,
                                ::distfs::StatusResponse *) override;

private:
  MetadataStore &store_;
  RaftNode &raft_;
  int dead_threshold_sec_;
  int replication_factor_;

  std::atomic<bool> running_{false};
  std::thread heartbeat_monitor_thread_;
  std::thread gc_monitor_thread_;

  std::map<std::string, std::vector<LocalNodePlacement>> pending_placements_;
  std::mutex pending_mu_;

  void heartbeat_monitor_loop();
  void gc_monitor_loop();
  void trigger_re_replication(const NodeID &dead_node);
  void cleanup_orphans();

  // Helper: return FAILED_PRECONDITION with leader hint if not leader
  grpc::Status require_leader() const;
};

} // namespace distfs
