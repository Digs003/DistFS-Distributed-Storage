#!/bin/bash
set -e

# Configuration
TEST_DIR="Test_distfs"
TEMP_FILES_DIR="test_files"
REPORT_FILE="$TEST_DIR/test_report.md"
COMPOSE_FILE="$TEST_DIR/docker-compose.yml"
CLI_BIN="/app/distfs/build/distfs-cli"
CONFIG_ARG="--config=/app/distfs/cluster.docker.conf"

echo "=== Starting DistFS Docker Test with Report Generation ==="

# Initialize Report
echo "# DistFS Test Report" > "$REPORT_FILE"
echo "Generated on: $(date)" >> "$REPORT_FILE"
echo "" >> "$REPORT_FILE"

# 1. Initialization
echo "## 1. Initialization" >> "$REPORT_FILE"
echo "Cleaning up previous test runs..."
docker compose -f "$COMPOSE_FILE" down --remove-orphans >> /dev/null 2>&1

echo "Building Docker images..."
BUILD_OUT=$(docker compose -f "$COMPOSE_FILE" build 2>&1)
echo "### Build Log" >> "$REPORT_FILE"
echo "\`\`\`" >> "$REPORT_FILE"
echo "$BUILD_OUT" | tail -n 20 >> "$REPORT_FILE"
echo "\`\`\`" >> "$REPORT_FILE"
echo "" >> "$REPORT_FILE"

# 2. Start Cluster & Election
echo "## 2. Cluster Startup & Leader Election" >> "$REPORT_FILE"
echo "Launching 3-node cluster..."
docker compose -f "$COMPOSE_FILE" up -d
echo "Waiting for cluster to stabilize (10s)..."
sleep 10

echo "Checking initial cluster status..."
STATUS_OUT=$(docker compose -f "$COMPOSE_FILE" exec client "$CLI_BIN" status $CONFIG_ARG)
echo "\`\`\`" >> "$REPORT_FILE"
echo "$STATUS_OUT" >> "$REPORT_FILE"
echo "\`\`\`" >> "$REPORT_FILE"
echo "" >> "$REPORT_FILE"

# 3. Generate 10 test files
echo "## 3. Test File Generation" >> "$REPORT_FILE"
mkdir -p "$TEMP_FILES_DIR"
formats=("txt" "bin" "log" "dat" "csv" "md" "json" "xml" "conf" "tmp")
echo "| File Name | Size | Format |" >> "$REPORT_FILE"
echo "|-----------|------|--------|" >> "$REPORT_FILE"

for i in {1..10}; do
    size=$(( (RANDOM % 5) + 1 )) # 1 to 5 MB
    ext=${formats[$((i-1))]}
    filename="file_$i.$ext"
    file_path="$TEMP_FILES_DIR/$filename"
    dd if=/dev/urandom of="$file_path" bs=1M count=$size 2>/dev/null
    echo "| $filename | ${size}MB | $ext |" >> "$REPORT_FILE"
done
echo "" >> "$REPORT_FILE"

# 4. Upload files
echo "## 4. File Upload & Chunk Replication" >> "$REPORT_FILE"
for i in {1..10}; do
    ext=${formats[$((i-1))]}
    filename="file_$i.$ext"
    local_path="$TEMP_FILES_DIR/$filename"
    
    echo "### Uploading $filename" >> "$REPORT_FILE"
    echo "Local path: \`$local_path\`" >> "$REPORT_FILE"
    
    # Copy file to client container
    docker cp "$local_path" "$(docker compose -f "$COMPOSE_FILE" ps -q client):/tmp/$filename"
    
    # Execute upload and capture output
    echo "Uploading $filename..."
    UPLOAD_OUT=$(docker compose -f "$COMPOSE_FILE" exec client "$CLI_BIN" upload --file "/tmp/$filename" --name "$filename" $CONFIG_ARG 2>&1)
    
    echo "\`\`\`" >> "$REPORT_FILE"
    echo "$UPLOAD_OUT" >> "$REPORT_FILE"
    echo "\`\`\`" >> "$REPORT_FILE"
    echo "" >> "$REPORT_FILE"
done

# 5. File List Verification
echo "## 5. Metadata Verification (List)" >> "$REPORT_FILE"
LIST_OUT=$(docker compose -f "$COMPOSE_FILE" exec client "$CLI_BIN" list $CONFIG_ARG)
echo "\`\`\`" >> "$REPORT_FILE"
echo "$LIST_OUT" >> "$REPORT_FILE"
echo "\`\`\`" >> "$REPORT_FILE"
echo "" >> "$REPORT_FILE"

# 6. Delete File & Verify
DELETE_FILE="file_5.csv"
echo "## 6. Deletion Test: $DELETE_FILE" >> "$REPORT_FILE"

# Before deletion, get metadata to know which chunks to check for
META_OUT=$(docker compose -f "$COMPOSE_FILE" exec client "$CLI_BIN" download --name "$DELETE_FILE" --out /tmp/meta_check --config=/app/distfs/cluster.docker.conf 2>&1 || true)
# Actually just list it from status to see orphaned count or use GetFileMetadata if I could easily.
# For simplicity, we'll just check if it's gone from list first.

echo "Executing deletion of $DELETE_FILE..."
docker compose -f "$COMPOSE_FILE" exec client "$CLI_BIN" delete --name "$DELETE_FILE" $CONFIG_ARG

echo "Verifying it's gone from the file list:" >> "$REPORT_FILE"
LIST_AFTER=$(docker compose -f "$COMPOSE_FILE" exec client "$CLI_BIN" list $CONFIG_ARG)
echo "\`\`\`" >> "$REPORT_FILE"
echo "$LIST_AFTER" >> "$REPORT_FILE"
echo "\`\`\`" >> "$REPORT_FILE"
echo "" >> "$REPORT_FILE"

echo "Checking storage nodes for residual chunks..." >> "$REPORT_FILE"
echo "Note: The system marks chunks as orphaned. Physical deletion is handled by a pending GC process." >> "$REPORT_FILE"

for node in node0 node1 node2; do
    echo "### Node: $node Storage Listing" >> "$REPORT_FILE"
    LIST_PHYS=$(docker compose -f "$COMPOSE_FILE" exec $node ls -R /var/distfs/chunks 2>/dev/null || echo "Directory empty or not found")
    echo "\`\`\`" >> "$REPORT_FILE"
    echo "$LIST_PHYS" >> "$REPORT_FILE"
    echo "\`\`\`" >> "$REPORT_FILE"
    echo "" >> "$REPORT_FILE"
done

# 7. Final Cluster Status
echo "## 7. Final Cluster Status" >> "$REPORT_FILE"
FINAL_STATUS=$(docker compose -f "$COMPOSE_FILE" exec client "$CLI_BIN" status $CONFIG_ARG)
echo "\`\`\`" >> "$REPORT_FILE"
echo "$FINAL_STATUS" >> "$REPORT_FILE"
echo "\`\`\`" >> "$REPORT_FILE"

echo "=== Test Completed. Report generated at $REPORT_FILE ==="

# 11. Cleanup (Optional: keep report, but stop containers)
echo "Cleaning up Docker resources (stopping containers)..."
docker compose -f "$COMPOSE_FILE" down
rm -rf "$TEMP_FILES_DIR"

echo "Done."
