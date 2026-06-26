#!/usr/bin/env bash
set -euo pipefail

# 啟動 ai-bmc-manager。預設以前景模式執行；傳入 background 時改用背景模式並寫入 PID / log。
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
BUILD_DIR="${REPO_ROOT}/build"
RUNTIME_DIR="${REPO_ROOT}/run"
BIN_PATH="${BUILD_DIR}/ai-bmc-manager"
CONFIG_PATH="${REPO_ROOT}/config/ai_server_profile.json"
PID_FILE="${RUNTIME_DIR}/ai-bmc-manager.pid"
LOG_FILE="${RUNTIME_DIR}/ai-bmc-manager.log"
MODE="foreground"

if [[ "${1:-}" == "background" ]]; then
  MODE="background"
  shift
fi

mkdir -p "${RUNTIME_DIR}"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "Binary not found at ${BIN_PATH}. Run scripts/02_build.sh first."
  exit 1
fi

cleanup_stale_pid_file() {
  # PID 檔只能當輔助資訊；若內容不是數字或行程已不存在，就先清掉。
  if [[ ! -f "${PID_FILE}" ]]; then
    return
  fi

  local pid
  pid=$(cat "${PID_FILE}")
  if [[ ! "${pid}" =~ ^[0-9]+$ ]] || ! kill -0 "${pid}" >/dev/null 2>&1; then
    rm -f "${PID_FILE}"
  fi
}

find_running_pid() {
  # 先信任有效 PID 檔，再用 pgrep 補查，避免 PID 檔遺失時重複啟動服務。
  if [[ -f "${PID_FILE}" ]]; then
    local pid
    pid=$(cat "${PID_FILE}")
    if [[ "${pid}" =~ ^[0-9]+$ ]] && kill -0 "${pid}" >/dev/null 2>&1; then
      echo "${pid}"
      return 0
    fi
  fi

  local pid
  while IFS= read -r pid; do
    if [[ -n "${pid}" ]] && kill -0 "${pid}" >/dev/null 2>&1; then
      echo "${pid}"
      return 0
    fi
  done < <(pgrep -f -- "${BIN_PATH}" 2>/dev/null || true)

  return 1
}

cleanup_stale_pid_file

if EXISTING_PID=$(find_running_pid); then
  echo "ai-bmc-manager is already running (PID: ${EXISTING_PID}). Stop it with scripts/99_cleanup.sh first."
  exit 1
fi

if [[ "${MODE}" == "background" ]]; then
  nohup "${BIN_PATH}" --config "${CONFIG_PATH}" "$@" > "${LOG_FILE}" 2>&1 &
  PID=$!
  sleep 1

  # 子行程建立成功不代表服務初始化成功；等待一下再確認行程仍存活。
  if kill -0 "${PID}" >/dev/null 2>&1; then
    echo "${PID}" > "${PID_FILE}"
    echo "Started in background. PID: ${PID}"
    echo "Log file: ${LOG_FILE}"
    exit 0
  fi

  STATUS=1
  if ! wait "${PID}"; then
    STATUS=$?
  fi

  echo "Failed to start ai-bmc-manager in background. Check ${LOG_FILE} for details." >&2
  if [[ -s "${LOG_FILE}" ]]; then
    tail -n 20 "${LOG_FILE}" >&2
  fi
  exit "${STATUS}"
fi

exec "${BIN_PATH}" --config "${CONFIG_PATH}" "$@"
