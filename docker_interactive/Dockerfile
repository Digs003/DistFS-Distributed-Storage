FROM ubuntu:22.04

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies from distfs/install_deps.sh and other useful tools
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    cmake \
    ninja-build \
    pkg-config \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    protobuf-compiler \
    libprotobuf-dev \
    libssl-dev \
    libfuse3-dev \
    fuse3 \
    tmux \
    wireshark-common \
    tcpdump \
    gdb \
    clang-format \
    clang-tidy \
    libgtest-dev \
    fio \
    net-tools \
    nmap \
    git \
    ca-certificates \
    vim \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /distfs

# Set default command to bash
CMD ["/bin/bash"]
