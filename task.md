# DistFS — Implementation Task Checklist

## WEEK 1 — Bottom-Up Infrastructure

### Day 1 — Repo, Build System & LAN Verification `[infra]`
- [x] Initialize git repo with directory skeleton (`distfs/` with subdirs)
- [x] Write root [CMakeLists.txt](file:///Users/digantamandal/Documents/DistFS/distfs/CMakeLists.txt) (3 targets: `metadata_server`, `storage_daemon`, `distfs-cli`)
- [ ] Install all dependencies on both laptops via `install_deps.sh`
- [ ] Verify LAN connectivity (static IPs, ufw ports 5000–5002 / 6001–6002, `nc` tests)
- [x] Write [cluster.conf](file:///Users/digantamandal/Documents/DistFS/distfs/cluster.conf) and [common/config.hpp](file:///Users/digantamandal/Documents/DistFS/distfs/common/config.hpp) / [config.cpp](file:///Users/digantamandal/Documents/DistFS/distfs/common/config.cpp) parser

### Day 2 — Proto Schema & gRPC Scaffolding `[grpc]`
- [x] Write [proto/distfs.proto](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto) (MetadataService, StorageService, RaftService + all messages)
- [x] Wire `protoc` into CMake (`grpc_generate_cpp()`, confirm stubs compile)
- [ ] Hello-world gRPC ping over LAN (server port 9999, client Ping RPC)

### Day 3 — SHA-256, WAL & Chunk I/O `[storage]`
- [x] Implement [common/sha256.hpp](file:///Users/digantamandal/Documents/DistFS/distfs/common/sha256.hpp) / [sha256.cpp](file:///Users/digantamandal/Documents/DistFS/distfs/common/sha256.cpp) (OpenSSL EVP [hash_file](file:///Users/digantamandal/Documents/DistFS/distfs/common/sha256.hpp#7-10) + [hash_bytes](file:///Users/digantamandal/Documents/DistFS/distfs/common/sha256.cpp#18-36))
- [ ] Unit-test SHA-256 with known vectors
- [x] Implement [common/wal.hpp](file:///Users/digantamandal/Documents/DistFS/distfs/common/wal.hpp) / [wal.cpp](file:///Users/digantamandal/Documents/DistFS/distfs/common/wal.cpp) (append-only binary log, `fsync`, crash-safe replay)
- [ ] Test WAL: write 100 entries, re-open, replay & verify
- [x] Implement [storage_daemon/chunk_store.hpp](file:///Users/digantamandal/Documents/DistFS/distfs/storage_daemon/chunk_store.hpp) / [chunk_store.cpp](file:///Users/digantamandal/Documents/DistFS/distfs/storage_daemon/chunk_store.cpp)
- [ ] Unit-test chunk_store (4 MB write → read → re-hash → delete)

### Day 4 — Storage Daemon gRPC Service `[storage]`
- [ ] Implement `storage_daemon/storage_service.hpp / storage_service.cpp` (UploadChunk, DownloadChunk, HasChunk, DeleteChunk)
- [ ] Implement `storage_daemon/main.cpp` (parse config, start gRPC server, SIGINT shutdown)
- [ ] Manual test: upload/download 20 MB file to one daemon, verify bytes

### Day 5 — Chain Replication: ForwardChunk & ReplicateChunk `[storage]`
- [ ] Implement [ForwardChunk](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#24-25) RPC (write locally → forward to secondary → wait ACK → return ACK)
- [ ] Implement [ReplicateChunk](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#25-26) RPC (read from local → ForwardChunk to target)
- [ ] End-to-end chain replication test (2 daemons, kill primary, read from secondary)

### Day 6 — Raft: Persistent State & Log `[raft]`
- [ ] Implement `raft/raft_log.hpp / raft_log.cpp` (WAL-backed log, append/get/truncate)
- [ ] Implement `raft/raft_node.hpp` (persistent + volatile state struct)
- [ ] Implement [RaftService](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#32-36) gRPC server stub (stub handlers, no logic yet)
- [ ] Unit-test `raft_log` (100 entries, new instance replay, verify)

### Day 7 — Raft: Leader Election `[raft]`
- [ ] Implement election timer thread (random 150–300 ms, resets on heartbeat/vote)
- [ ] Implement [RequestVote](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#33-34) RPC handler (term check, log-ok check, vote grant)
- [ ] Implement vote response collector (majority → LEADER, init `next_index`/`match_index`)
- [ ] Implement heartbeat sender thread (50 ms, step down on higher term)
- [ ] Election smoke test: 3 nodes locally → exactly one leader, leader failover within 600 ms

---

## WEEK 2 — Integration, Top-Down

### Day 8 — Raft: Log Replication `[raft]`
- [ ] Implement [AppendEntries](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#34-35) handler — follower side (term check, log consistency, truncate/append, commit)
- [ ] Implement [AppendEntries](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#34-35) sender — leader side (build entries, track `next_index`/`match_index`, commit_index update)
- [ ] Implement `ClientRequest` on leader (append → replicate → block on commit → apply)
- [ ] Log replication correctness test (3-node, 50 cmds, kill follower@30, 20 more, restart, verify 70 entries)

### Day 9 — Metadata Server: State Machine & RPCs `[grpc]`
- [ ] Implement `metadata_server/metadata_store.hpp / metadata_store.cpp` (`file_map`, `chunk_map`, `node_registry`)
- [ ] Implement [InitiateUpload](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#8-9) RPC (validate, select primary/secondary by least-used)
- [ ] Implement [CommitUpload](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#9-10) RPC (Raft ClientRequest, update maps)
- [ ] Implement [GetFileMetadata](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#10-11) RPC
- [ ] Implement [ListFiles](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#11-12) RPC
- [ ] Implement [DeleteFile](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#12-13) RPC

### Day 10 — Heartbeats & Failure Detection `[storage/infra]`
- [ ] Implement heartbeat client in storage daemon (`storage_daemon/heartbeat_client.cpp`, 3 s interval)
- [ ] Implement [RegisterHeartbeat](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#13-14) RPC on metadata server (leader-only, update `node_registry`)
- [ ] Implement `HeartbeatMonitor` thread (wakes every 3 s, marks DEAD at 9 s threshold)
- [ ] Implement `TriggerReReplication(dead_node_id)` (find under-replicated chunks, issue ReplicateChunk)
- [ ] Failure detection test (kill daemon-A, wait 10 s, verify DEAD log + re-replication)

### Day 11 — CLI Client: Chunking & Upload/Download `[cli]`
- [ ] Implement `client/chunker.hpp / chunker.cpp` (`split_file` 4 MB + SHA-256, `reassemble_file`)
- [ ] Implement `client/uploader.cpp` (`upload_file`: chunk → InitiateUpload → stream chunks → CommitUpload)
- [ ] Implement `client/downloader.cpp` (`download_file`: GetFileMetadata → stream chunks → reassemble → verify)
- [ ] Implement `client/main.cpp` (subcommand dispatch: upload/download/list/delete/status)

### Day 12 — CLI: Remaining Commands & GetClusterStatus `[cli]`
- [ ] Implement [list](file:///Users/digantamandal/Documents/DistFS/distfs/common/config.cpp#23-40) command (ListFiles RPC, formatted table output)
- [ ] Implement [delete](file:///Users/digantamandal/Documents/DistFS/distfs/storage_daemon/chunk_store.cpp#79-84) command (DeleteFile RPC)
- [ ] Implement `status` command (GetClusterStatus RPC, formatted output)
- [ ] Implement [GetClusterStatus](file:///Users/digantamandal/Documents/DistFS/distfs/proto/distfs.proto#14-15) RPC on metadata server (aggregate Raft + node_registry + chunk stats)

### Day 13 — Full Integration & LAN Demo Setup `[demo]`
- [ ] Write `launch_laptop_a.sh` (tmux: node-0, node-2, daemon-A, CLI pane)
- [ ] Write `launch_laptop_b.sh` (tmux: node-1, daemon-B, CLI pane)
- [ ] End-to-end test: upload 50 MB → download on Laptop B → `diff` (byte-identical)
- [ ] Repeat with 1 MB, 50 MB, 200 MB files
- [ ] Concurrent upload stress test (4 simultaneous uploads, verify list shows all 4)
- [ ] Raft leader failover demo (kill leader → upload succeeds within 500 ms → verify)
- [ ] Storage daemon failure demo (upload 5 files, kill daemon-A, download all 5 succeed)

### Day 14 — Hardening, Edge Cases & Documentation `[infra/demo]`
- [ ] Handle split-brain: follower returns `FAILED_PRECONDITION(leader_hint)`, CLI retries ×3
- [ ] Handle storage daemon unreachable during upload (fallback node, abort if both down)
- [ ] Implement basic chunk GC scan (60 s periodic, log orphaned chunks)
- [ ] Write `README.md` (prereqs, network setup, build, launch, 5 demo scenarios)
- [ ] Final full rehearsal from cold start (time all scenarios against targets)

---

## Critical Path (do NOT defer)
```
Day 3: WAL + raft_log  →  Day 6: Raft state
Day 7: Leader election →  Day 8: Log replication
Day 8: Log replication →  Day 9: Metadata state machine
Day 9: Metadata RPCs   →  Day 11: CLI upload/download
Day 5: ForwardChunk    →  Day 10: Failure detection
```
