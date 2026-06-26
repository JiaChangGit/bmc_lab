#!/usr/bin/env bash
set -euo pipefail

# 停止背景模式啟動的 ai-bmc-manager，並清掉執行期 PID / log。
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
RUNTIME_DIR="${REPO_ROOT}/run"
PID_FILE="${RUNTIME_DIR}/ai-bmc-manager.pid"
BIN_PATH="${REPO_ROOT}/build/ai-bmc-manager"

declare -A PIDS_TO_STOP=()

if [[ -f "${PID_FILE}" ]]; then
  PID=$(cat "${PID_FILE}")
  if [[ "${PID}" =~ ^[0-9]+$ ]]; then
    PIDS_TO_STOP["${PID}"]=1
  fi
fi

# PID 檔若遺失或過期，改用 pgrep 依主程式路徑補查一次。
while IFS= read -r PID; do
  if [[ -n "${PID}" ]]; then
    PIDS_TO_STOP["${PID}"]=1
  fi
done < <(pgrep -f -- "${BIN_PATH}" 2>/dev/null || true)

for PID in "${!PIDS_TO_STOP[@]}"; do
  if kill -0 "${PID}" >/dev/null 2>&1; then
    kill "${PID}"
    echo "Stopped process ${PID}."
  fi
done

rm -f "${PID_FILE}"

rm -f "${RUNTIME_DIR}/ai-bmc-manager.log"
echo "Cleanup complete."
