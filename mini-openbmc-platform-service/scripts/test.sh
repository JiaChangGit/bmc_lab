#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if [[ ! -d build ]]; then
    echo "Build directory is missing. Run scripts/build.sh first." >&2
    exit 1
fi

ctest --test-dir build --output-on-failure
