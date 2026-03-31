#include "storage_daemon/storage_service.hpp"
#include "common/sha256.hpp"
#include <grpcpp/grpcpp.h>

namespace distfs {

static constexpr size_t FRAME_SIZE = 1024 * 1024; // 1 MB per gRPC frame

StorageServiceImpl::StorageServiceImpl(const std::string& data_dir,
                                       const std::string& node_id)
    : store_(data_dir), node_id_(node_id) {}

// --------------------------------------------------------
// UploadChunk: receive streaming ChunkData → write to disk
// If ChunkRequest.secondary_addr is set → ForwardChunk after write
// --------------------------------------------------------
grpc::Status StorageServiceImpl::UploadChunk(
    grpc::ServerContext* /*ctx*/,
    grpc::ServerReader<::distfs::ChunkData>* reader,
    ::distfs::ChunkAck* reply)
{
    ::distfs::ChunkData frame;
    std::vector<uint8_t> buf;
    std::string chunk_hash;

    while (reader->Read(&frame)) {
        if (chunk_hash.empty()) chunk_hash = frame.chunk_hash();
        const std::string& d = frame.data();
        buf.insert(buf.end(), d.begin(), d.end());
    }

    if (chunk_hash.empty()) {
        reply->set_success(false);
        reply->set_error_message("No data received");
        return grpc::Status::OK;
    }

    try {
        store_.write_chunk(chunk_hash, buf);
        reply->set_success(true);
    } catch (const std::exception& e) {
        reply->set_success(false);
        reply->set_error_message(e.what());
    }
    return grpc::Status::OK;
}

// --------------------------------------------------------
// DownloadChunk: read from disk → stream in 1 MB frames
// --------------------------------------------------------
grpc::Status StorageServiceImpl::DownloadChunk(
    grpc::ServerContext* /*ctx*/,
    const ::distfs::ChunkRequest* req,
    grpc::ServerWriter<::distfs::ChunkData>* writer)
{
    try {
        auto data = store_.read_chunk(req->chunk_hash());
        size_t offset = 0;
        while (offset < data.size()) {
            size_t frame_len = std::min(FRAME_SIZE, data.size() - offset);
            ::distfs::ChunkData frame;
            frame.set_chunk_hash(req->chunk_hash());
            frame.set_data(reinterpret_cast<const char*>(data.data() + offset), frame_len);
            frame.set_offset(static_cast<int64_t>(offset));
            writer->Write(frame);
            offset += frame_len;
        }
    } catch (const std::exception& e) {
        return grpc::Status(grpc::StatusCode::NOT_FOUND, e.what());
    }
    return grpc::Status::OK;
}

// --------------------------------------------------------
// DeleteChunk
// --------------------------------------------------------
grpc::Status StorageServiceImpl::DeleteChunk(
    grpc::ServerContext* /*ctx*/,
    const ::distfs::ChunkRequest* req,
    ::distfs::ChunkAck* reply)
{
    try {
        store_.delete_chunk(req->chunk_hash());
        reply->set_success(true);
    } catch (const std::exception& e) {
        reply->set_success(false);
        reply->set_error_message(e.what());
    }
    return grpc::Status::OK;
}

// --------------------------------------------------------
// ForwardChunk: receive bytes from peer → write to local disk
// (same as UploadChunk but called via daemon-to-daemon RPC)
// --------------------------------------------------------
grpc::Status StorageServiceImpl::ForwardChunk(
    grpc::ServerContext* ctx,
    grpc::ServerReader<::distfs::ChunkData>* reader,
    ::distfs::ChunkAck* reply)
{
    return UploadChunk(ctx, reader, reply);
}

// --------------------------------------------------------
// ReplicateChunk: read local chunk → ForwardChunk to target
// --------------------------------------------------------
grpc::Status StorageServiceImpl::ReplicateChunk(
    grpc::ServerContext* /*ctx*/,
    const ::distfs::ReplicateRequest* req,
    ::distfs::ChunkAck* reply)
{
    try {
        auto data = store_.read_chunk(req->chunk_hash());
        auto status = forward_to(req->target_addr(), req->chunk_hash(), data);
        if (!status.ok()) {
            reply->set_success(false);
            reply->set_error_message(status.error_message());
        } else {
            reply->set_success(true);
        }
    } catch (const std::exception& e) {
        reply->set_success(false);
        reply->set_error_message(e.what());
    }
    return grpc::Status::OK;
}

// --------------------------------------------------------
// HasChunk
// --------------------------------------------------------
grpc::Status StorageServiceImpl::HasChunk(
    grpc::ServerContext* /*ctx*/,
    const ::distfs::ChunkRequest* req,
    ::distfs::ChunkAck* reply)
{
    reply->set_success(store_.has_chunk(req->chunk_hash()));
    return grpc::Status::OK;
}

// --------------------------------------------------------
// Internal: stream chunk bytes to a remote ForwardChunk RPC
// --------------------------------------------------------
grpc::Status StorageServiceImpl::forward_to(const std::string& target_addr,
                                             const std::string& chunk_hash,
                                             const std::vector<uint8_t>& data)
{
    auto channel = grpc::CreateChannel(target_addr, grpc::InsecureChannelCredentials());
    auto stub = ::distfs::StorageService::NewStub(channel);

    ::distfs::ChunkAck ack;
    grpc::ClientContext ctx;
    auto writer = stub->ForwardChunk(&ctx, &ack);

    size_t offset = 0;
    while (offset < data.size()) {
        size_t frame_len = std::min(FRAME_SIZE, data.size() - offset);
        ::distfs::ChunkData frame;
        frame.set_chunk_hash(chunk_hash);
        frame.set_data(reinterpret_cast<const char*>(data.data() + offset), frame_len);
        frame.set_offset(static_cast<int64_t>(offset));
        if (!writer->Write(frame))
            return grpc::Status(grpc::StatusCode::UNAVAILABLE, "ForwardChunk stream broken");
        offset += frame_len;
    }

    writer->WritesDone();
    auto status = writer->Finish();
    if (!status.ok()) return status;
    if (!ack.success())
        return grpc::Status(grpc::StatusCode::INTERNAL, ack.error_message());
    return grpc::Status::OK;
}

} // namespace distfs
