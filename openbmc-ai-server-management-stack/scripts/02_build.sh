#!/usr/bin/env bash
set -euo pipefail

# 從專案根目錄產生 Ninja 建置檔、編譯主程式，最後執行單元測試。
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
BUILD_DIR="${REPO_ROOT}/build"

cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "Build and unit tests completed successfully."
