#include "metadata_server/metadata_store.hpp"
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>

namespace distfs {

// ============================================================
// Raft command serialization helpers (simple tag-length-value)
// ============================================================
// Format: [ type: 1 byte ] [ payload bytes ]
// Types: 1=commit_upload, 2=delete_file, 3=update_chunk_map

static void write_str(std::vector<uint8_t>& buf, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    buf.resize(buf.size() + 4 + len);
    std::memcpy(buf.data() + buf.size() - 4 - len, &len, 4);
    std::memcpy(buf.data() + buf.size() - len, s.data(), len);
}
static std::string read_str(const uint8_t* p, size_t& off) {
    uint32_t len; std::memcpy(&len, p + off, 4); off += 4;
    std::string s(reinterpret_cast<const char*>(p + off), len); off += len;
    return s;
}

// ---- CommitUpload command ----
std::vector<uint8_t> MetadataStore::cmd_commit_upload(
    const std::string& filename, int64_t revision_id, int64_t total_bytes,
    const std::vector<std::string>& chunk_hashes,
    const std::vector<LocalNodePlacement>& placements)
{
    std::vector<uint8_t> buf;
    buf.push_back(1); // type
    write_str(buf, filename);
    buf.resize(buf.size() + 8); std::memcpy(buf.data() + buf.size() - 8, &revision_id, 8);
    buf.resize(buf.size() + 8); std::memcpy(buf.data() + buf.size() - 8, &total_bytes, 8);
    uint32_t nc = static_cast<uint32_t>(chunk_hashes.size());
    buf.resize(buf.size() + 4); std::memcpy(buf.data() + buf.size() - 4, &nc, 4);
    for (auto& h : chunk_hashes) write_str(buf, h);
    // placements: chunk_hash, primary_node, secondary_node
    for (auto& p : placements) {
        write_str(buf, p.chunk_hash);
        write_str(buf, p.primary_node);
        write_str(buf, p.secondary_node);
    }
    return buf;
}

std::vector<uint8_t> MetadataStore::cmd_delete_file(const std::string& filename) {
    std::vector<uint8_t> buf;
    buf.push_back(2);
    write_str(buf, filename);
    return buf;
}

std::vector<uint8_t> MetadataStore::cmd_update_chunk_map(
    const std::string& chunk_hash, const std::vector<NodeID>& nodes) {
    std::vector<uint8_t> buf;
    buf.push_back(3);
    write_str(buf, chunk_hash);
    uint32_t n = static_cast<uint32_t>(nodes.size());
    buf.resize(buf.size() + 4); std::memcpy(buf.data() + buf.size() - 4, &n, 4);
    for (auto& nid : nodes) write_str(buf, nid);
    return buf;
}

// ============================================================
// Apply a committed Raft command
// ============================================================
void MetadataStore::apply_command(const std::vector<uint8_t>& command) {
    if (command.empty()) return;
    uint8_t type = command[0];
    if      (type == 1) apply_commit_upload({command.begin()+1, command.end()});
    else if (type == 2) apply_delete_file  ({command.begin()+1, command.end()});
    else if (type == 3) apply_update_chunk_map({command.begin()+1, command.end()});
}

void MetadataStore::apply_commit_upload(const std::vector<uint8_t>& p) {
    const uint8_t* data = p.data(); size_t off = 0;
    std::string filename = read_str(data, off);
    int64_t revision_id; std::memcpy(&revision_id, data + off, 8); off += 8;
    int64_t total_bytes;  std::memcpy(&total_bytes, data + off, 8); off += 8;
    uint32_t nc; std::memcpy(&nc, data + off, 4); off += 4;
    std::vector<std::string> hashes(nc);
    for (auto& h : hashes) h = read_str(data, off);

    std::lock_guard<std::mutex> lk(mu_);
    FileRecord rec;
    rec.filename       = filename;
    rec.revision_id    = revision_id;
    rec.total_size_bytes = total_bytes;
    rec.chunk_hashes   = hashes;
    file_map_[filename] = rec;

    // placements: triplets of (chunk_hash, primary, secondary)
    while (off < p.size()) {
        std::string ch    = read_str(data, off);
        std::string pnode = read_str(data, off);
        std::string snode = read_str(data, off);
        auto& nodes = chunk_map_[ch];
        if (std::find(nodes.begin(), nodes.end(), pnode) == nodes.end())
            nodes.push_back(pnode);
        if (!snode.empty() && std::find(nodes.begin(), nodes.end(), snode) == nodes.end())
            nodes.push_back(snode);
    }
}

void MetadataStore::apply_delete_file(const std::vector<uint8_t>& p) {
    const uint8_t* data = p.data(); size_t off = 0;
    std::string filename = read_str(data, off);
    std::lock_guard<std::mutex> lk(mu_);
    file_map_.erase(filename);
    // chunks remain as orphans in chunk_map (GC handled separately)
}

void MetadataStore::apply_update_chunk_map(const std::vector<uint8_t>& p) {
    const uint8_t* data = p.data(); size_t off = 0;
    std::string ch = read_str(data, off);
    uint32_t n; std::memcpy(&n, data + off, 4); off += 4;
    std::vector<NodeID> nodes(n);
    for (auto& nid : nodes) nid = read_str(data, off);
    std::lock_guard<std::mutex> lk(mu_);
    chunk_map_[ch] = nodes;
}

// ============================================================
// Read-path
// ============================================================
bool MetadataStore::file_exists(const std::string& filename) const {
    std::lock_guard<std::mutex> lk(mu_);
    return file_map_.count(filename) > 0;
}
FileRecord MetadataStore::get_file(const std::string& filename) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = file_map_.find(filename);
    if (it == file_map_.end()) throw std::runtime_error("File not found: " + filename);
    return it->second;
}
std::vector<FileRecord> MetadataStore::list_files() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<FileRecord> v;
    for (auto& [k, r] : file_map_) v.push_back(r);
    return v;
}
std::vector<NodeID> MetadataStore::get_chunk_nodes(const std::string& h) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = chunk_map_.find(h);
    return it == chunk_map_.end() ? std::vector<NodeID>{} : it->second;
}

// ---- Node selection: least-used alive node ----
NodeID MetadataStore::select_primary() const {
    // Caller does NOT hold mu_ here (called from service methods that hold mu)
    NodeID best; int64_t best_used = INT64_MAX;
    for (auto& [id, info] : node_registry_) {
        if (info.status == NodeInfo::Status::ALIVE && info.used_bytes < best_used) {
            best_used = info.used_bytes; best = id;
        }
    }
    return best;
}
NodeID MetadataStore::select_secondary(const NodeID& exclude) const {
    NodeID best; int64_t best_used = INT64_MAX;
    for (auto& [id, info] : node_registry_) {
        if (id == exclude) continue;
        if (info.status == NodeInfo::Status::ALIVE && info.used_bytes < best_used) {
            best_used = info.used_bytes; best = id;
        }
    }
    return best;
}

void MetadataStore::register_heartbeat(const ::distfs::HeartbeatRequest& req) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& info = node_registry_[req.node_id()];
    info.node_id     = req.node_id();
    info.address     = req.address();
    info.used_bytes  = req.used_bytes();
    info.total_bytes = req.total_bytes();
    info.chunk_count = req.chunk_count();
    info.last_heartbeat = std::time(nullptr);
    if (info.status == NodeInfo::Status::DEAD) {
        info.status = NodeInfo::Status::ALIVE;
        // Log recovery
    }
}

std::vector<NodeInfo> MetadataStore::alive_nodes() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<NodeInfo> v;
    for (auto& [id, info] : node_registry_)
        if (info.status == NodeInfo::Status::ALIVE) v.push_back(info);
    return v;
}
std::vector<NodeInfo> MetadataStore::all_nodes() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<NodeInfo> v;
    for (auto& [id, info] : node_registry_) v.push_back(info);
    return v;
}

std::string MetadataStore::node_address(const NodeID& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = node_registry_.find(id);
    return it == node_registry_.end() ? "" : it->second.address;
}

void MetadataStore::mark_dead(const NodeID& id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = node_registry_.find(id);
    if (it != node_registry_.end())
        it->second.status = NodeInfo::Status::DEAD;
}

int64_t MetadataStore::total_chunks() const {
    std::lock_guard<std::mutex> lk(mu_);
    return static_cast<int64_t>(chunk_map_.size());
}
int64_t MetadataStore::under_replicated(int rf_threshold) const {
    std::lock_guard<std::mutex> lk(mu_);
    int64_t count = 0;
    for (auto& [h, nodes] : chunk_map_)
        if (static_cast<int>(nodes.size()) < rf_threshold) ++count;
    return count;
}
int64_t MetadataStore::orphaned_chunks() const {
    // Chunks in chunk_map not referenced by any file
    std::lock_guard<std::mutex> lk(mu_);
    std::unordered_map<std::string, bool> referenced;
    for (auto& [name, rec] : file_map_)
        for (auto& h : rec.chunk_hashes)
            referenced[h] = true;
    int64_t count = 0;
    for (auto& [h, nodes] : chunk_map_)
        if (!referenced.count(h)) ++count;
    return count;
}

} // namespace distfs
