# GEMINI.md - DistFS Project Context

## Project Overview
**DistFS** is a distributed storage system implemented in C++17. It features a decentralized metadata layer using the Raft consensus algorithm, a chunk-based storage architecture with chain replication, and a gRPC-based communication protocol.

### Key Components
- **Metadata Server (`metadata_server`)**: Manages file-to-chunk mappings and storage node registry. Uses **Raft** for high availability and consistency.
- **Storage Daemon (`storage_daemon`)**: Stores data chunks on disk. Supports chunk uploads, downloads, and replication (Forwarding/Replicating).
- **Client CLI (`distfs-cli`)**: Handles file chunking (4MB default), SHA-256 hashing, and coordinates with metadata and storage nodes for file operations.
- **Raft Library (`distfs_raft`)**: Custom implementation of the Raft consensus algorithm for leader election and log replication.
- **Common Library (`distfs_common`)**: Shared utilities including SHA-256 hashing (OpenSSL), a crash-safe Write-Ahead Log (WAL), and configuration parsing.

### Technology Stack
- **Language**: C++17
- **Communication**: gRPC / Protocol Buffers
- **Security/Hashing**: OpenSSL (SHA-256)
- **Consensus**: Raft
- **Build System**: CMake
- **Testing**: GoogleTest (GTest)

---

## Change Management Mandate
- **Logging**: EVERY change made to files within the `distfs/` directory (modifications, creations, or deletions) MUST be logged in `change.txt` at the root of the project.
- **Reversion**: The log in `change.txt` must serve as a record to track all modifications, allowing for a systematic reversion to the original state if requested.

---

## Building and Running

### Prerequisites
- Ubuntu 22.04+ (or compatible Linux/macOS with gRPC/Protobuf/OpenSSL)
- Dependencies can be installed via:
  ```bash
  bash distfs/install_deps.sh
  ```

### Build Commands
```bash
cd distfs
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```
*Note: Use `-DCMAKE_BUILD_TYPE=Debug` to enable AddressSanitizer and debug symbols.*

### Running Tests
```bash
ctest --test-dir build --output-on-failure
```

### Launching the Cluster
Refer to `distfs/launch_laptop_a.sh` and `distfs/launch_laptop_b.sh` for multi-node setup.
Basic manual startup requires a `cluster.conf` file defining node IDs and addresses.

---

## Development Conventions

### Code Style
- **Namespaces**: All code resides in the `distfs` namespace.
- **Files**: `.hpp` for headers, `.cpp` for implementations.
- **Naming**: `snake_case` for functions and variables, `PascalCase` for classes and types.
- **Persistence**: All metadata and Raft logs must use the `WAL` class to ensure crash-safety via `fsync`.

### Architecture & Protocol
- **Service Definitions**: Defined in `distfs/proto/distfs.proto`. This is the source of truth for all network interactions.
- **Error Handling**: gRPC status codes are used for RPC errors. Leader redirection uses `FAILED_PRECONDITION` with a leader hint.
- **Chunking**: Files are split into 4MB chunks, each identified by its SHA-256 hash.

### Task Management
Refer to `task.md` for the current implementation status and roadmap. The project follows a "Bottom-Up" infrastructure approach followed by "Top-Down" integration.

---

## Key Files
- `distfs/proto/distfs.proto`: gRPC service and message definitions.
- `distfs/raft/raft_node.hpp`: Raft consensus implementation logic.
- `distfs/metadata_server/metadata_service.hpp`: Implementation of the Metadata gRPC service.
- `distfs/storage_daemon/chunk_store.hpp`: Local disk management for data chunks.
- `distfs/common/wal.hpp`: Write-Ahead Log for persistent state.
- `distfs/cluster.conf`: Cluster topology configuration.
