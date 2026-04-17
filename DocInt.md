# Interactive Docker Development Guide

This guide explains how to set up and use the interactive Docker environment for DistFS, starting from a fresh clone of the repository.

## 1. Prerequisites
Ensure you have the following installed on your host machine:
- [Docker](https://docs.docker.com/get-docker/)
- [Docker Compose](https://docs.docker.com/compose/install/)
- `git`

---

## 2. Setup from Scratch

### Clone the Repository
```bash
git clone <repository-url>
cd DistFS-Distributed-Storage
```

### Build the Docker Image
Navigate to the `docker_interactive` directory and build the development container:
```bash
cd docker_interactive
docker compose build distfs-shell
```

---

## 3. Entering the Interactive Shell

Run the following command to start the container and enter its bash shell:
```bash
docker compose run --rm distfs-shell
```
*Note: This command mounts your local source code into the container. Any changes you make to the code on your host machine will be reflected immediately inside the shell.*

---

## 4. Building DistFS Inside the Shell

Once inside the container (`/distfs`), run these commands to compile the project:

```bash
cd distfs
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

---

## 5. Running Tests

### Unit Tests
To run the automated test suite:
```bash
ctest --test-dir build --output-on-failure
```

### Manual Integration Test (Minimal Cluster)
To verify the full system (Metadata + Storage + CLI):

1. **Prepare directories:**
   ```bash
   mkdir -p /tmp/distfs/wal /tmp/distfs/chunks
   ```

2. **Launch Metadata Server (Background):**
   ```bash
   ./build/metadata_server --id=node-0 --port=5000 --wal=/tmp/distfs/wal --config=cluster.conf &
   ```

3. **Launch Storage Daemon (Background):**
   ```bash
   ./build/storage_daemon --id=daemon-A --port=6001 --data_dir=/tmp/distfs/chunks --config=cluster.conf &
   ```

4. **Verify with CLI:**
   ```bash
   ./build/distfs-cli status --config=cluster.conf
   ```

---

## 6. Tips for Efficient Development
- **Persistence:** The binaries built in the `build/` folder are saved on your host machine because of the volume mount. You don't need to recompile everything if you restart the container.
- **Multiple Windows:** The image includes `tmux`. Run `tmux` inside the shell to split your screen and watch logs while running CLI commands.
- **Network:** The container uses `network_mode: host`, so services bound to `127.0.0.1` inside the container are accessible to other processes in the same container and the host.
