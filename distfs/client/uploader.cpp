#include "proto_gen/distfs.grpc.pb.h"
#include "client/chunker.hpp"
#include "common/sha256.hpp"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace distfs {

static constexpr size_t FRAME_SIZE = 1024 * 1024; // 1 MB gRPC frame

/// Upload a file to DistFS.
/// meta_stub: connected stub to the Metadata Server (leader).
void upload_file(const std::string& local_path,
                 const std::string& remote_name,
                 ::distfs::MetadataService::Stub& meta_stub,
                 int64_t chunk_size) {
    // ── Step 1: Chunk the file ──────────────────────────────────────────────
    std::cout << "[1/4] Chunking: " << local_path << "\n";
    auto chunks = split_file(local_path, chunk_size);
    for (auto& c : chunks)
        std::cout << "      Chunk " << c.chunk_index << ": sha256=" << c.chunk_hash.substr(0,8)
                  << "... (" << c.size_bytes / (1024*1024.0) << " MB)\n";

    // ── Step 2: InitiateUpload ──────────────────────────────────────────────
    std::cout << "[2/4] Contacting Metadata Server...\n";
    ::distfs::InitiateUploadRequest init_req;
    init_req.set_filename(remote_name);
    for (auto& c : chunks) {
        auto* ci = init_req.add_chunks();
        ci->set_chunk_hash(c.chunk_hash);
        ci->set_size_bytes(c.size_bytes);
        ci->set_chunk_index(c.chunk_index);
    }
    ::distfs::InitiateUploadResponse init_resp;
    grpc::ClientContext init_ctx;
    auto st = meta_stub.InitiateUpload(&init_ctx, init_req, &init_resp);
    if (!st.ok()) throw std::runtime_error("InitiateUpload failed: " + st.error_message());
    std::cout << "      Received placement plan (" << init_resp.placements_size() << " chunks)\n";

    // ── Step 3: Upload chunks to storage daemons ────────────────────────────
    std::cout << "[3/4] Uploading chunks...\n";
    std::ifstream file(local_path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot reopen: " + local_path);

    for (int i = 0; i < init_resp.placements_size(); ++i) {
        const auto& pl = init_resp.placements(i);
        std::string addr = pl.primary_addr().empty() ? pl.primary_node() : pl.primary_addr();

        auto chan = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        auto stub = ::distfs::StorageService::NewStub(chan);
        ::distfs::ChunkAck ack;
        grpc::ClientContext ctx;
        auto writer = stub->UploadChunk(&ctx, &ack);

        // Read chunk bytes
        int64_t remaining = chunks[i].size_bytes;
        std::vector<char> buf(FRAME_SIZE);
        int64_t offset = 0;
        while (remaining > 0) {
            auto to_read = std::min((int64_t)FRAME_SIZE, remaining);
            file.read(buf.data(), to_read);
            ::distfs::ChunkData frame;
            frame.set_chunk_hash(pl.chunk_hash());
            frame.set_data(buf.data(), file.gcount());
            frame.set_offset(offset);

            // Set secondary replica address for forwarding by the primary daemon
            if (!pl.secondary_addr().empty() || !pl.secondary_node().empty()) {
                std::string sec_addr = pl.secondary_addr().empty() ? pl.secondary_node() : pl.secondary_addr();
                frame.set_secondary_addr(sec_addr);
            }
            
            writer->Write(frame);
            offset += file.gcount();
            remaining -= file.gcount();
        }
        writer->WritesDone();
        auto finish_st = writer->Finish();
        if (!finish_st.ok() || !ack.success())
            throw std::runtime_error("Chunk upload failed: " + ack.error_message());
        std::cout << "      Chunk " << i << " → " << pl.primary_node() << " [OK]\n";
    }

    // ── Step 4: CommitUpload ────────────────────────────────────────────────
    std::cout << "[4/4] Committing metadata...\n";
    ::distfs::CommitUploadRequest commit_req;
    commit_req.set_filename(remote_name);
    commit_req.set_upload_token(init_resp.upload_token());
    for (auto& c : chunks) {
        auto* ci = commit_req.add_chunks();
        ci->set_chunk_hash(c.chunk_hash);
        ci->set_size_bytes(c.size_bytes);
        ci->set_chunk_index(c.chunk_index);
    }
    ::distfs::CommitUploadResponse commit_resp;
    grpc::ClientContext commit_ctx;
    auto cst = meta_stub.CommitUpload(&commit_ctx, commit_req, &commit_resp);
    if (!cst.ok() || !commit_resp.success())
        throw std::runtime_error("CommitUpload failed: " + commit_resp.error());
    std::cout << "      File registered. Revision: " << commit_resp.revision_id() << "\n";
    std::cout << "Upload complete. " << chunks.size() << " chunks, 2 replicas each.\n";
}

} // namespace distfs
