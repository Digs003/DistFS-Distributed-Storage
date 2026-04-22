#pragma once
#include "storage_daemon/chunk_store.hpp"
#include "proto_gen/distfs.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <string>

namespace distfs {

class StorageServiceImpl final : public ::distfs::StorageService::Service {
public:
    explicit StorageServiceImpl(const std::string& data_dir,
                                const std::string& node_id);

    // Upload a chunk (streaming from client)
    grpc::Status UploadChunk(grpc::ServerContext* ctx,
                             grpc::ServerReader<::distfs::ChunkData>* reader,
                             ::distfs::ChunkAck* reply) override;

    // Download a chunk (streaming to client)
    grpc::Status DownloadChunk(grpc::ServerContext* ctx,
                               const ::distfs::ChunkRequest* req,
                               grpc::ServerWriter<::distfs::ChunkData>* writer) override;

    // Delete a chunk from local disk
    grpc::Status DeleteChunk(grpc::ServerContext* ctx,
                             const ::distfs::ChunkRequest* req,
                             ::distfs::ChunkAck* reply) override;

    // Forward chunk to another daemon (chain replication leg)
    grpc::Status ForwardChunk(grpc::ServerContext* ctx,
                              grpc::ServerReader<::distfs::ChunkData>* reader,
                              ::distfs::ChunkAck* reply) override;

    // Orchestrate re-replication: read local chunk → send to target
    grpc::Status ReplicateChunk(grpc::ServerContext* ctx,
                                const ::distfs::ReplicateRequest* req,
                                ::distfs::ChunkAck* reply) override;

    // Check if a chunk exists locally
    grpc::Status HasChunk(grpc::ServerContext* ctx,
                          const ::distfs::ChunkRequest* req,
                          ::distfs::ChunkAck* reply) override;

    // Local helper to get disk stats
    void get_stats(int64_t& used_bytes, int64_t& total_bytes, int64_t& chunk_count) const;

private:
    ChunkStore  store_;
    std::string node_id_;

    // Helper: stream chunk bytes to a remote StorageService::ForwardChunk RPC
    grpc::Status forward_to(const std::string& target_addr,
                            const std::string& chunk_hash,
                            const std::vector<uint8_t>& data);
};

} // namespace distfs
