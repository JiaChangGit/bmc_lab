#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

if [[ ! -f "${REPO_ROOT}/CMakeLists.txt" ]]; then
  echo "找不到 CMakeLists.txt，無法確認目前目錄是否為專案根目錄。"
  exit 1
fi

# 先停掉仍在執行的背景服務，避免刪除 run/ 時還有程式行程持續寫入。
"${SCRIPT_DIR}/99_cleanup.sh"

TARGETS=(
  "${REPO_ROOT}/build"
  "${REPO_ROOT}/run"
)

for target in "${TARGETS[@]}"; do
  if [[ -e "${target}" ]]; then
    rm -rf "${target}"
    echo "已刪除 ${target}"
  fi
done

echo "清理完成。建置產物與執行期產物已移除。"
