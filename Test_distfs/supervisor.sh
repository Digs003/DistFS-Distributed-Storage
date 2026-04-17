#!/bin/bash

# Configuration
CONFIG_PATH="/app/distfs/cluster.docker.conf"
BIN_DIR="/app/distfs/build"

# Ensure directories exist
mkdir -p "$DATA_DIR"
mkdir -p "$WAL_DIR"

echo "Starting DistFS processes for Node ID: $NODE_ID"

# Map NODE_ID (node-0, node-1, node-2) to storage IDs (daemon-A, daemon-B, daemon-C)
if [[ "$NODE_ID" == "node-0" ]]; then
    DAEMON_ID="daemon-A"
elif [[ "$NODE_ID" == "node-1" ]]; then
    DAEMON_ID="daemon-B"
elif [[ "$NODE_ID" == "node-2" ]]; then
    DAEMON_ID="daemon-C"
else
    echo "Unknown Node ID: $NODE_ID"
    exit 1
fi

# Start Metadata Server in the background
echo "Launching Metadata Server ($NODE_ID) on port $META_PORT..."
"$BIN_DIR/metadata_server" \
    --id="$NODE_ID" \
    --port="$META_PORT" \
    --wal="$WAL_DIR" \
    --config="$CONFIG_PATH" \
    --verbose &
META_PID=$!

# Start Storage Daemon in the background
# We pass --address=nodeX:PORT so it reports a reachable address in heartbeats,
# but we still bind to 0.0.0.0 via --port.
if [[ "$NODE_ID" == "node-0" ]]; then
    SERVICE_HOST="node0"
elif [[ "$NODE_ID" == "node-1" ]]; then
    SERVICE_HOST="node1"
elif [[ "$NODE_ID" == "node-2" ]]; then
    SERVICE_HOST="node2"
fi

echo "Launching Storage Daemon ($DAEMON_ID) on $SERVICE_HOST:$STORAGE_PORT..."
"$BIN_DIR/storage_daemon" \
    --id="$DAEMON_ID" \
    --port="$STORAGE_PORT" \
    --address="$SERVICE_HOST:$STORAGE_PORT" \
    --data-dir="$DATA_DIR" \
    --config="$CONFIG_PATH" \
    --verbose &
STORAGE_PID=$!

# Function to handle shutdown
cleanup() {
    echo "Shutting down processes..."
    kill $META_PID
    kill $STORAGE_PID
    wait $META_PID
    wait $STORAGE_PID
    exit 0
}

# Trap signals
trap cleanup SIGINT SIGTERM

# Wait for processes
wait $META_PID $STORAGE_PID
