// Forward declarations matching the implementations in uploader.cpp / downloader.cpp
#include "proto_gen/distfs.grpc.pb.h"
#include "common/config.hpp"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>

namespace distfs {
void upload_file(const std::string&, const std::string&, ::distfs::MetadataService::Stub&, int64_t);
void download_file(const std::string&, const std::string&, ::distfs::MetadataService::Stub&);
}

#include <chrono>

static std::unique_ptr<::distfs::MetadataService::Stub>
connect_metadata(const std::vector<std::string>& nodes, bool requires_leader) {
    std::string last_err;
    std::unique_ptr<::distfs::MetadataService::Stub> alive_stub;

    for (auto& addr : nodes) {
        auto chan = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
        auto stub = ::distfs::MetadataService::NewStub(chan);

        if (requires_leader) {
            ::distfs::InitiateUploadRequest req;
            req.set_filename(".ping_leader");
            ::distfs::InitiateUploadResponse resp;
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(2000));
            auto st = stub->InitiateUpload(&ctx, req, &resp);

            if (st.error_code() == grpc::StatusCode::FAILED_PRECONDITION) {
                if (!alive_stub) alive_stub = ::distfs::MetadataService::NewStub(chan);
                continue; // Not leader
            } else if (!st.ok()) {
                last_err = st.error_message();
                continue; // Node unavailable or timeout, try next node
            } else {
                // We reached the leader safely
                return stub;
            }
        } else {
            ::distfs::StatusRequest req;
            ::distfs::StatusResponse resp;
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(2000));
            auto st = stub->GetClusterStatus(&ctx, req, &resp);
            if (st.ok()) return stub;
            last_err = st.error_message();
        }
    }
    
    if (alive_stub && !requires_leader) return alive_stub;

    throw std::runtime_error("Failed to connect to cluster. Last err: " + last_err);
}

// ── status ──────────────────────────────────────────────────────────────────
static void cmd_status(::distfs::MetadataService::Stub& stub) {
    ::distfs::StatusRequest req;
    ::distfs::StatusResponse resp;
    grpc::ClientContext ctx;
    auto st = stub.GetClusterStatus(&ctx, req, &resp);
    if (!st.ok()) { std::cerr << "status failed: " << st.error_message() << "\n"; return; }
    std::cout << "\n=== DistFS Cluster Status ===\n";
    std::cout << "Metadata Cluster:\n";
    std::cout << "  Leader:    " << resp.leader_id() << "\n";
    std::cout << "  Term:      " << resp.current_term() << " | Log Index: " << resp.log_index() << "\n";
    std::cout << "Storage Daemons:\n";
    for (auto& n : resp.storage_nodes()) {
        std::cout << "  " << n.address()
                  << "  [" << (n.is_alive() ? "ALIVE" : "DEAD") << "]"
                  << "  " << (int)(n.used_bytes()/1048576.0) << " MB / "
                  << (int)(n.total_bytes()/1048576.0) << " MB\n";
    }
    std::cout << "Replication Health:\n";
    std::cout << "  Total chunks: " << resp.total_chunks()
              << "   Under-replicated: " << resp.under_replicated()
              << "   Orphaned: " << resp.orphaned_chunks() << "\n\n";
}

// ── list ─────────────────────────────────────────────────────────────────────
static void cmd_list(::distfs::MetadataService::Stub& stub) {
    ::distfs::ListFilesRequest req;
    ::distfs::ListFilesResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(2000));
    auto st = stub.ListFiles(&ctx, req, &resp);
    if (!st.ok()) { std::cerr << "list failed: " << st.error_message() << "\n"; return; }
    if (resp.files_size() == 0) { std::cout << "No files stored.\n"; return; }
    std::cout << std::left
              << std::setw(30) << "NAME"
              << std::setw(10) << "CHUNKS"
              << "SIZE\n";
    std::cout << std::string(54, '-') << "\n";
    for (auto& f : resp.files()) {
        double mb = f.total_bytes() / 1048576.0;
        std::cout << std::left
                  << std::setw(30) << f.filename()
                  << std::setw(10) << f.chunk_count()
                  << (std::to_string((int)mb) + " MB") << "\n";
    }
}

// ── delete ───────────────────────────────────────────────────────────────────
static void cmd_delete(::distfs::MetadataService::Stub& stub, const std::string& name) {
    ::distfs::DeleteFileRequest req;
    req.set_filename(name);
    ::distfs::DeleteFileResponse resp;
    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(2000));
    auto st = stub.DeleteFile(&ctx, req, &resp);
    if (!st.ok() || !resp.success())
        std::cerr << "delete failed: " << resp.error() << "\n";
    else
        std::cout << "Deleted: " << name << " (chunks orphaned, GC will clean up periodically)\n";
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: distfs-cli <upload|download|list|delete|status> [options]\n"
                  << "  upload   --file=<path> --name=<remote_name>\n"
                  << "  download --name=<remote_name> --out=<local_path>\n"
                  << "  list\n"
                  << "  delete   --name=<remote_name>\n"
                  << "  status\n";
        return 1;
    }

    std::string subcmd = argv[1];
    std::string file_path, remote_name, out_path;
    std::string config_path = "remote_cluster.conf";

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--file=", 0) == 0) file_path = a.substr(7);
        else if (a == "--file" && i + 1 < argc) file_path = argv[++i];
        else if (a.rfind("--name=", 0) == 0) remote_name = a.substr(7);
        else if (a == "--name" && i + 1 < argc) remote_name = argv[++i];
        else if (a.rfind("--out=", 0) == 0) out_path = a.substr(6);
        else if (a == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (a.rfind("--config=", 0) == 0) config_path = a.substr(9);
        else if (a == "--config" && i + 1 < argc) config_path = argv[++i];
    }

    distfs::ClusterConfig cfg;
    try { cfg = distfs::parse_config(config_path); }
    catch (const std::exception& e) {
        std::cerr << "[ERROR] Config: " << e.what() << "\n"; return 1;
    }

    try {
        if (subcmd == "upload") {
            if (file_path.empty() || remote_name.empty()) {
                std::cerr << "upload requires --file and --name\n"; return 1;
            }
            auto stub = connect_metadata(cfg.client.metadata_nodes, true);
            distfs::upload_file(file_path, remote_name, *stub, cfg.storage.chunk_size_bytes);
        } else if (subcmd == "download") {
            if (remote_name.empty() || out_path.empty()) {
                std::cerr << "download requires --name and --out\n"; return 1;
            }
            auto stub = connect_metadata(cfg.client.metadata_nodes, true);
            distfs::download_file(remote_name, out_path, *stub);
        } else if (subcmd == "list") {
            auto stub = connect_metadata(cfg.client.metadata_nodes, true);
            cmd_list(*stub);
        } else if (subcmd == "delete") {
            if (remote_name.empty()) { std::cerr << "delete requires --name\n"; return 1; }
            auto stub = connect_metadata(cfg.client.metadata_nodes, true);
            cmd_delete(*stub, remote_name);
        } else if (subcmd == "status") {
            auto stub = connect_metadata(cfg.client.metadata_nodes, true);
            cmd_status(*stub);
        } else {
            std::cerr << "Unknown subcommand: " << subcmd << "\n"; return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n"; return 1;
    }
    return 0;
}
