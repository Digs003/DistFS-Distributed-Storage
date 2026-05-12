# DistFS — Distributed File Storage System

DistFS is a distributed file storage service inspired by Ceph and Dropbox's Magic Pocket architecture. It is implemented entirely in C++ and designed to run across Linux nodes over a Local Area Network (LAN). It also supports simulating an entire cluster on a single machine using process isolation via `tmux`.

The system separates two fundamental concerns of any distributed storage service:
- **The Control Plane (Metadata):** Manages file-to-chunk mappings and cluster coordination. Handled by a cluster of Metadata Servers running a custom implementation of the Raft consensus protocol.
- **The Data Plane (Chunks):** Manages the actual binary data. Handled by Storage Daemons that store 4 MB binary chunks as flat files on the local filesystem using Content-Addressable Storage and chain replication.

## Architecture

* **CLI Client (`distfs-cli`):** The user-facing tool to upload, download, list, delete files, and check cluster status. It chunks files, hashes them, issues RPCs to the Metadata Server, and coordinates data transfer with Storage Daemons.
* **Metadata Server:** The control plane process. It stores the metadata (file mappings), runs the Raft protocol to ensure high availability and correctness, and monitors storage daemons via heartbeats.
* **Storage Daemon:** The data plane process. It stores and serves binary chunks, performs synchronous chain replication for fault tolerance, and sends regular heartbeats to the leader Metadata Server.

## Prerequisites (Ubuntu 22.04+)

Dependencies must be installed on all participating machines:
```bash
bash install_deps.sh
```

## Network Setup (For LAN deployment)

Example setup for two laptops:
- **Laptop A:** `192.168.1.10`
- **Laptop B:** `192.168.1.11`

Open the required ports for gRPC communication and raft consensus:
```bash
sudo ufw allow 5000,5001,5002,6001,6002/tcp
ping 192.168.1.11   # Verify LAN connectivity
```

## Build Instructions

DistFS uses CMake. Build the project using the following commands:
```bash
cd distfs
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Running Unit Tests

To run the unit tests provided by CTest:
```bash
ctest --test-dir build --output-on-failure
```

## Launch (LAN Demo)

DistFS includes scripts to launch a multi-node deployment via tmux.

**Laptop A:**
```bash
sudo mkdir -p /var/distfs/{wal0,wal2,chunks}
bash launch_laptop_a.sh
# This launches two metadata servers (node-0, node-2), a storage daemon (daemon-A), and a CLI pane.
```

**Laptop B:**
```bash
sudo mkdir -p /var/distfs/{wal1,chunks}
bash launch_laptop_b.sh
# This launches one metadata server (node-1), a storage daemon (daemon-B), and a CLI pane.
```

## Usage & Core Commands

Interact using `distfs-cli`. Core commands include:

```bash
# Upload a file
./build/distfs-cli upload --file /path/to/video.mp4 --name "lecture.mp4"

# Download a file
./build/distfs-cli download --name "lecture.mp4" --out /tmp/lecture.mp4

# List all stored files
./build/distfs-cli list

# Delete a file
./build/distfs-cli delete --name "lecture.mp4"

# Check cluster status (Raft leader, replication health)
./build/distfs-cli status
```

## Fault Tolerance Scenarios

### 1. Leader Failover
```bash
./build/distfs-cli status                # Identify the current leader
pkill -f "metadata_server --id=node-0"   # Terminate the leader process
./build/distfs-cli upload --file test.bin --name test # Upload should succeed within ~500ms
./build/distfs-cli status                # Observe the newly elected leader
```

### 2. Storage Daemon Failure
```bash
./build/distfs-cli status                # Note daemon state
pkill -9 -f "storage_daemon --id=daemon-A" # Terminate a storage daemon
./build/distfs-cli download --name file.bin --out /tmp/dl.bin # Download still works via replica
sleep 10
./build/distfs-cli status                # System automatically detects failure and triggers re-replication
```

## Performance Targets

| Scenario | Target |
|---|---|
| Upload 50 MB | < 5 s |
| Download 50 MB | < 5 s |
| Leader failover | < 500 ms |
| Re-replication after failure | < 15 s |
