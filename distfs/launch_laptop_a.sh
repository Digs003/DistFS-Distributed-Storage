#!/bin/bash
# launch_laptop_a.sh — Run on Laptop A (192.168.1.10)
# Starts: metadata_server node-0 (:5000), node-2 (:5002), storage_daemon daemon-A (:6001)

BUILD="$(dirname "$0")/build"

tmux new-session -d -s distfs -x 220 -y 50

# Pane 0: metadata_server node-0 (initial Raft leader candidate)
tmux send-keys -t distfs:0 \
  "$BUILD/metadata_server --id=node-0 --port=5000 --wal=/var/distfs/wal0 --config=$(dirname $0)/cluster.conf" C-m

# Pane 1: metadata_server node-2 (second Raft node on Laptop A)
tmux split-window -h -t distfs:0
tmux send-keys -t distfs:0.1 \
  "$BUILD/metadata_server --id=node-2 --port=5002 --wal=/var/distfs/wal2 --config=$(dirname $0)/cluster.conf" C-m

# Pane 2: storage_daemon daemon-A
tmux split-window -v -t distfs:0.0
tmux send-keys -t distfs:0.2 \
  "$BUILD/storage_daemon --id=daemon-A --port=6001 --data-dir=/var/distfs/chunks --config=$(dirname $0)/cluster.conf" C-m

# Pane 3: CLI ready
tmux split-window -v -t distfs:0.1
tmux send-keys -t distfs:0.3 "echo 'CLI ready. Run: $BUILD/distfs-cli --help'" Enter

tmux attach -t distfs
