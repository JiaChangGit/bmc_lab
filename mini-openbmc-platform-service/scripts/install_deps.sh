#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "This script requires Ubuntu Linux." >&2
    exit 1
fi

echo "Installing Mini OpenBMC build dependencies..."
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    g++ \
    pkg-config \
    libsystemd-dev \
    curl \
    nlohmann-json3-dev \
    libgtest-dev \
    libcpp-httplib-dev \
    dbus \
    jq

kernel_headers="linux-headers-$(uname -r)"
if apt-cache show "$kernel_headers" >/dev/null 2>&1; then
    sudo apt-get install -y "$kernel_headers"
else
    echo "Kernel headers package $kernel_headers is unavailable."
    echo "Installing generic headers for compile-only kernel module validation."
    sudo apt-get install -y linux-headers-generic
    echo "Modules built with generic headers cannot be loaded into a different running kernel."
fi

echo "Dependencies installed. CMake will use apt packages when available and FetchContent otherwise."
