#!/usr/bin/env bash
set -euo pipefail

# 可用 BASE_URL 覆寫目標服務位址，預設連到本機 Redfish 風格 HTTP API。
BASE_URL="${BASE_URL:-http://127.0.0.1:8080}"

pretty_print() {
  # Demo 優先使用 jq 排版；沒安裝 jq 時仍保留原始 JSON，避免腳本直接中斷。
  if command -v jq >/dev/null 2>&1; then
    jq .
  else
    cat
  fi
}

echo "GET ${BASE_URL}/redfish/v1"
curl -s "${BASE_URL}/redfish/v1" | pretty_print
echo

echo "GET ${BASE_URL}/redfish/v1/Systems/system"
curl -s "${BASE_URL}/redfish/v1/Systems/system" | pretty_print
echo

echo "GET ${BASE_URL}/redfish/v1/Chassis/chassis/Thermal"
curl -s "${BASE_URL}/redfish/v1/Chassis/chassis/Thermal" | pretty_print
echo

echo "GET ${BASE_URL}/redfish/v1/Chassis/chassis/Power"
curl -s "${BASE_URL}/redfish/v1/Chassis/chassis/Power" | pretty_print
echo

echo "GET ${BASE_URL}/redfish/v1/Managers/bmc/LogServices/EventLog/Entries"
curl -s "${BASE_URL}/redfish/v1/Managers/bmc/LogServices/EventLog/Entries" | pretty_print
echo

echo "GET ${BASE_URL}/redfish/v1/UpdateService"
curl -s "${BASE_URL}/redfish/v1/UpdateService" | pretty_print
echo

echo "POST ${BASE_URL}/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate"
curl -s -X POST \
  -H "Content-Type: application/json" \
  -d '{"image_uri":"file:///tmp/fw-demo-good.bin"}' \
  "${BASE_URL}/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate" | pretty_print
echo
