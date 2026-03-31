#!/bin/bash
# install_deps.sh — Run on BOTH laptops (Ubuntu/Debian)
set -e

sudo apt update && sudo apt install -y \
  g++ cmake ninja-build \
  libgrpc++-dev grpc-proto \
  protobuf-compiler libprotobuf-dev \
  libssl-dev \
  libfuse3-dev fuse3 \
  tmux \
  wireshark tcpdump \
  gdb \
  clang-format clang-tidy \
  libgtest-dev \
  fio \
  net-tools nmap

echo "All dependencies installed successfully."
