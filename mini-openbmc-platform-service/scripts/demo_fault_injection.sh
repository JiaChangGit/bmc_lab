#!/usr/bin/env bash
set -euo pipefail

base="${MINI_BMC_URL:-http://127.0.0.1:8080}"
body='{"target":"GPU0_Core_Temp","fault":"out_of_range","enabled":true}'

echo "Injecting GPU0 core temperature fault through Redfish and D-Bus..."
curl --fail --silent -X POST "$base/debug/faults" \
    -H "Content-Type: application/json" -d "$body" | jq .
sleep 2
echo "Sensor after fault:"
curl --fail --silent \
    "$base/redfish/v1/Chassis/GPU0/Sensors/GPU0_Core_Temp" | jq .
echo "Event log after fault:"
curl --fail --silent \
    "$base/redfish/v1/Systems/System0/LogServices/EventLog/Entries" | jq .
echo "Clearing fault..."
curl --fail --silent -X POST "$base/debug/faults" \
    -H "Content-Type: application/json" \
    -d '{"target":"GPU0_Core_Temp","fault":"out_of_range","enabled":false}' | jq .
