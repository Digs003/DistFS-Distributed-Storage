#include "proto_gen/distfs.grpc.pb.h"
#include "client/chunker.hpp"
#include "common/sha256.hpp"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <stdexcept>

namespace distfs {

static constexpr size_t FRAME_SIZE = 1024 * 1024;

void download_file(const std::string& remote_name,
                   const std::string& out_path,
                   ::distfs::MetadataService::Stub& meta_stub) {
    // ── Step 1: GetFileMetadata ─────────────────────────────────────────────
    std::cout << "[1/3] Fetching chunk map for: " << remote_name << "\n";
    ::distfs::GetFileMetadataRequest req;
    req.set_filename(remote_name);
    ::distfs::GetFileMetadataResponse resp;
    grpc::ClientContext ctx;
    auto st = meta_stub.GetFileMetadata(&ctx, req, &resp);
    if (!st.ok()) throw std::runtime_error("GetFileMetadata failed: " + st.error_message());
    if (!resp.found()) throw std::runtime_error("File not found: " + remote_name);
    const auto& meta = resp.metadata();
    std::cout << "      " << meta.chunks_size() << " chunks\n";

    // ── Step 2: Download each chunk to a temp file ──────────────────────────
    std::cout << "[2/3] Fetching chunks...\n";
    std::vector<std::string> tmp_paths;
    for (int i = 0; i < meta.chunks_size(); ++i) {
        const auto& ci   = meta.chunks(i);
        const auto& pl   = meta.placements(i);
        std::string paddr = pl.primary_addr().empty() ? pl.primary_node() : pl.primary_addr();
        std::string saddr = pl.secondary_addr().empty() ? pl.secondary_node() : pl.secondary_addr();
        std::string tmp  = out_path + ".chunk" + std::to_string(i) + ".tmp";
        bool ok = false;

        std::vector<std::string> err_msgs;
        for (const std::string& try_addr : {paddr, saddr}) {
            if (try_addr.empty()) {
                err_msgs.push_back("empty address");
                continue;
            }
            try {
                auto chan = grpc::CreateChannel(try_addr, grpc::InsecureChannelCredentials());
                auto stub = ::distfs::StorageService::NewStub(chan);
                ::distfs::ChunkRequest creq;
                creq.set_chunk_hash(ci.chunk_hash());
                grpc::ClientContext cctx;
                cctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
                auto reader = stub->DownloadChunk(&cctx, creq);
                std::ofstream tmp_file(tmp, std::ios::binary | std::ios::trunc);
                ::distfs::ChunkData frame;
                while (reader->Read(&frame))
                    tmp_file.write(frame.data().data(), frame.data().size());
                auto fst = reader->Finish();
                if (fst.ok()) { ok = true; break; }
                else { err_msgs.push_back(try_addr + ": " + fst.error_message()); }
            } catch (const std::exception& e) {
                err_msgs.push_back(try_addr + " exception: " + e.what());
            }
        }
        if (!ok) {
            std::string all_errs;
            for (auto& e : err_msgs) all_errs += "[" + e + "] ";
            throw std::runtime_error("Failed to download chunk " + std::to_string(i) + ". Errors: " + all_errs);
        }
        std::cout << "      Chunk " << i << " [OK]\n";
        tmp_paths.push_back(tmp);
    }

    // ── Step 3: Reassemble and verify ──────────────────────────────────────
    std::cout << "[3/3] Reassembling file...\n";
    std::string final_hash = reassemble_file(tmp_paths, out_path);
    // Cleanup temps
    for (auto& t : tmp_paths) std::remove(t.c_str());
    std::cout << "      SHA-256: " << final_hash.substr(0,16) << "...\n";
    std::cout << "Download complete → " << out_path << "\n";
}

} // namespace distfs
