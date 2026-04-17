FROM ubuntu:22.04

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libssl-dev \
    protobuf-compiler \
    libprotobuf-dev \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    pkg-config \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy the distfs source code
COPY distfs /app/distfs

# Build the project
RUN cd /app/distfs && \
    cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc)

# Copy the supervisor script
COPY Test_distfs/supervisor.sh /app/supervisor.sh
RUN chmod +x /app/supervisor.sh

# Environment variables for the processes
ENV NODE_ID=""
ENV META_PORT="5000"
ENV STORAGE_PORT="6001"
ENV DATA_DIR="/var/distfs/chunks"
ENV WAL_DIR="/var/distfs/wal"

# Default command
CMD ["/app/supervisor.sh"]
