#!/usr/bin/env bash
set -euo pipefail

base="${MINI_BMC_URL:-http://127.0.0.1:8080}"
echo "Service root:"
curl --fail --silent "$base/redfish/v1" | jq .
echo "GPU0 sensor collection:"
curl --fail --silent "$base/redfish/v1/Chassis/GPU0/Sensors" | jq .
echo "GPU0 core temperature:"
curl --fail --silent \
    "$base/redfish/v1/Chassis/GPU0/Sensors/GPU0_Core_Temp" | jq .
