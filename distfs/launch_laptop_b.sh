#!/bin/bash
# launch_laptop_b.sh — Run on Laptop B (192.168.1.11)
# Starts: metadata_server node-1 (:5001), storage_daemon daemon-B (:6002)

BUILD="$(dirname "$0")/build"

tmux new-session -d -s distfs -x 220 -y 50

# Pane 0: metadata_server node-1 (Raft follower)
tmux send-keys -t distfs:0 \
  "$BUILD/metadata_server --id=node-1 --port=5001 --wal=/var/distfs/wal1 --config=$(dirname $0)/cluster.conf" C-m

# Pane 1: storage_daemon daemon-B
tmux split-window -h -t distfs:0
tmux send-keys -t distfs:0.1 \
  "$BUILD/storage_daemon --id=daemon-B --port=6002 --data-dir=/var/distfs/chunks --config=$(dirname $0)/cluster.conf" C-m

# Pane 2: CLI ready
tmux split-window -v -t distfs:0.0
tmux send-keys -t distfs:0.2 "echo 'CLI ready. Run: $BUILD/distfs-cli --help'" Enter

tmux attach -t distfs
