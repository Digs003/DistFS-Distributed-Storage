#include "common/config.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace distfs {

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Split "k=v" into {k, v}
static std::pair<std::string, std::string> split_kv(const std::string& line) {
    auto eq = line.find('=');
    if (eq == std::string::npos) throw std::runtime_error("Malformed config line: " + line);
    return { trim(line.substr(0, eq)), trim(line.substr(eq + 1)) };
}

// "id@ip:port,id2@ip2:port2" → vector<{id, addr}>
static std::vector<std::pair<std::string,std::string>> parse_addr_list(const std::string& val) {
    std::vector<std::pair<std::string,std::string>> result;
    std::istringstream ss(val);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trim(token);
        auto at = token.find('@');
        if (at == std::string::npos) {
            // plain address, no id
            result.push_back({"", token});
        } else {
            result.push_back({token.substr(0, at), token.substr(at + 1)});
        }
    }
    return result;
}

ClusterConfig parse_config(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open config file: " + path);

    ClusterConfig cfg;
    std::string section;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        auto [key, val] = split_kv(line);

        if (section == "raft") {
            if (key == "nodes") {
                for (auto& [id, addr] : parse_addr_list(val))
                    cfg.raft.nodes.push_back({id, addr});
            } else if (key == "election_timeout_min_ms") {
                cfg.raft.election_timeout_min_ms = std::stoi(val);
            } else if (key == "election_timeout_max_ms") {
                cfg.raft.election_timeout_max_ms = std::stoi(val);
            } else if (key == "heartbeat_interval_ms") {
                cfg.raft.heartbeat_interval_ms = std::stoi(val);
            } else if (key == "rpc_timeout_ms") {
                cfg.raft.rpc_timeout_ms = std::stoi(val);
            }
        } else if (section == "storage") {
            if (key == "daemons") {
                for (auto& [id, addr] : parse_addr_list(val))
                    cfg.storage.daemons.push_back({id, addr});
            } else if (key == "chunk_size_bytes") {
                cfg.storage.chunk_size_bytes = std::stoll(val);
            } else if (key == "replication_factor") {
                cfg.storage.replication_factor = std::stoi(val);
            } else if (key == "dead_threshold_sec") {
                cfg.storage.dead_threshold_sec = std::stoi(val);
            }
        } else if (section == "client") {
            if (key == "metadata_nodes") {
                std::istringstream ss2(val);
                std::string tok;
                while (std::getline(ss2, tok, ','))
                    cfg.client.metadata_nodes.push_back(trim(tok));
            }
        }
    }

    if (cfg.raft.nodes.empty())
        throw std::runtime_error("Config error: [raft] nodes is empty");
    if (cfg.storage.daemons.empty())
        throw std::runtime_error("Config error: [storage] daemons is empty");

    return cfg;
}

} // namespace distfs
