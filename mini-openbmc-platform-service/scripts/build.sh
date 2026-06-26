#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

echo "Configuring userspace build..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
echo "Building userspace targets with four jobs..."
cmake --build build -j4
echo "Userspace build completed."
