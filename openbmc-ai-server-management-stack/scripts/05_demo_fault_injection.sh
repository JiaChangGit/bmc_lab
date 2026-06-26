#!/usr/bin/env bash
set -euo pipefail

# 這支腳本用固定順序製造故障，方便觀察 Thermal / Power / EventLog 是否同步變化。
BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"

pretty_print() {
  # Demo 優先使用 jq 排版；沒安裝 jq 時仍保留原始 JSON，避免腳本直接中斷。
  if command -v jq >/dev/null 2>&1; then
    jq .
  else
    cat
  fi
}

echo "Inject GPU over-temperature on gpu0"
curl -s -X POST "${BASE_URL}/api/fault/gpu-overtemp/gpu0" | pretty_print
echo
sleep 1

echo "Inject fan failure on fan0"
curl -s -X POST "${BASE_URL}/api/fault/fan-failure/fan0" | pretty_print
echo
sleep 1

echo "Inject PSU failure on psu0"
curl -s -X POST "${BASE_URL}/api/fault/psu-failure/psu0" | pretty_print
echo
sleep 1

echo "Inject NVMe fault on nvme0"
curl -s -X POST "${BASE_URL}/api/fault/nvme-fault/nvme0" | pretty_print
echo
sleep 1

echo "Escalate all GPUs to force the power cap path"
for gpu in gpu1 gpu2 gpu3 gpu4 gpu5 gpu6 gpu7; do
  curl -s -X POST "${BASE_URL}/api/fault/gpu-overtemp/${gpu}" > /dev/null
done
sleep 2

echo "GET ${BASE_URL}/redfish/v1/Chassis/chassis/Power"
curl -s "${BASE_URL}/redfish/v1/Chassis/chassis/Power" | pretty_print
echo

echo "GET ${BASE_URL}/redfish/v1/Managers/bmc/LogServices/EventLog/Entries"
curl -s "${BASE_URL}/redfish/v1/Managers/bmc/LogServices/EventLog/Entries" | pretty_print
echo

echo "Clear all faults"
curl -s -X POST "${BASE_URL}/api/fault/clear" | pretty_print
echo
