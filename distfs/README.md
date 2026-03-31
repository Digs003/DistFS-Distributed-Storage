# DistFS — README

## Prerequisites (both laptops, Ubuntu 22.04+)
```bash
bash install_deps.sh
```

## Network Setup
```bash
# Laptop A: 192.168.1.10  |  Laptop B: 192.168.1.11
sudo ufw allow 5000,5001,5002,6001,6002/tcp
ping 192.168.1.11   # verify LAN from Laptop A
```

## Build
```bash
cd distfs
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Run Unit Tests
```bash
ctest --test-dir build --output-on-failure
```

## Launch (LAN demo)
**Laptop A:**
```bash
sudo mkdir -p /var/distfs/{wal0,wal2,chunks}
bash launch_laptop_a.sh
```
**Laptop B:**
```bash
sudo mkdir -p /var/distfs/{wal1,chunks}
bash launch_laptop_b.sh
```

## Demo Scenarios

### 1. Basic upload / download / list
```bash
./build/distfs-cli upload --file /path/to/file.bin --name file.bin
./build/distfs-cli list
./build/distfs-cli download --name file.bin --out /tmp/file_dl.bin
diff /path/to/file.bin /tmp/file_dl.bin    # must be empty
```

### 2. Concurrent uploads (stress test)
```bash
# In 4 tmux panes simultaneously:
./build/distfs-cli upload --file fileA.bin --name A
./build/distfs-cli upload --file fileB.bin --name B
./build/distfs-cli upload --file fileC.bin --name C
./build/distfs-cli upload --file fileD.bin --name D
./build/distfs-cli list    # all 4 files must appear
```

### 3. Leader failover
```bash
./build/distfs-cli status   # note current leader
pkill -f "metadata_server --id=node-0"   # kill leader
./build/distfs-cli upload --file test.bin --name test   # must succeed within ~500ms
./build/distfs-cli status   # new leader elected
```

### 4. Storage daemon failure
```bash
./build/distfs-cli status   # check daemon state
pkill -9 -f "storage_daemon --id=daemon-A"
./build/distfs-cli download --name file.bin --out /tmp/dl.bin   # still works from daemon-B
sleep 10
./build/distfs-cli status   # under-replicated should be 0 if 3rd daemon available
```

### 5. FUSE mount (stretch)
```bash
# Requires implementing client/fuse_driver.cpp
./build/distfs-fuse /mnt/distfs --metadata=192.168.1.10:5000
ls /mnt/distfs
cp ~/new.pdf /mnt/distfs/
```

## Performance Targets
| Scenario | Target |
|---|---|
| Upload 50 MB | < 5 s |
| Download 50 MB | < 5 s |
| Leader failover | < 500 ms |
| Re-replication after failure | < 15 s |
