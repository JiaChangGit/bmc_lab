#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

build_dir="${MINI_BMC_BUILD_DIR:-build}"
base_url="${MINI_BMC_URL:-http://127.0.0.1:8080}"
output_dir="runtime/smoke-test"
session_log="$output_dir/session.log"
session_pid=""

for binary in \
    "$build_dir/services/pldm-endpoint-agent/pldm-endpoint-agent" \
    "$build_dir/services/bmc-sensor-service/bmc-sensor-service" \
    "$build_dir/services/redfish-service/redfish-service" \
    "$build_dir/tools/dbus_dump"; do
    if [[ ! -x "$binary" ]]; then
        echo "Missing binary: $binary" >&2
        echo "Run scripts/build.sh first." >&2
        exit 1
    fi
done

if ! command -v curl >/dev/null 2>&1 ||
    ! command -v jq >/dev/null 2>&1; then
    echo "curl and jq are required for the smoke test." >&2
    exit 1
fi

cleanup() {
    if [[ -n "$session_pid" ]]; then
        kill "$session_pid" 2>/dev/null || true
        wait "$session_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

rm -rf "$output_dir"
mkdir -p "$output_dir"

echo "Starting the full MiniBMC session..."
./scripts/run_session.sh >"$session_log" 2>&1 &
session_pid=$!

ready=false
for _ in $(seq 1 100); do
    if curl --silent --fail \
        "$base_url/redfish/v1/Chassis/GPU0/Sensors" \
        >"$output_dir/sensors.json"; then
        ready=true
        break
    fi
    if ! kill -0 "$session_pid" 2>/dev/null; then
        break
    fi
    sleep 0.1
done
if [[ "$ready" != true ]]; then
    echo "The MiniBMC session did not become ready." >&2
    cat "$session_log" >&2
    exit 1
fi

curl --silent --fail "$base_url/redfish/v1/Systems" \
    >"$output_dir/systems.json"
curl --silent --fail "$base_url/redfish/v1" \
    >"$output_dir/service-root.json"
curl --silent --fail "$base_url/redfish/v1/Chassis" \
    >"$output_dir/chassis.json"
curl --silent --fail "$base_url/redfish/v1/Chassis/GPU0" \
    >"$output_dir/chassis-gpu0.json"
curl --silent --fail "$base_url/redfish/v1/Systems/System0" \
    >"$output_dir/system0.json"
curl --silent --fail "$base_url/redfish/v1/Systems/System0/LogServices" \
    >"$output_dir/log-services.json"
curl --silent --fail \
    "$base_url/redfish/v1/Systems/System0/LogServices/EventLog" \
    >"$output_dir/event-log.json"
curl --silent --fail "$base_url/redfish/v1/Managers" \
    >"$output_dir/managers.json"
curl --silent --fail "$base_url/redfish/v1/Managers/BMC0" \
    >"$output_dir/manager-bmc0.json"
curl --silent --fail "$base_url/redfish/v1/Managers/BMC0/Health" \
    >"$output_dir/manager-health.json"
curl --silent --fail "$base_url/redfish/v1/Chassis/GPU0/PCIeDevices" \
    >"$output_dir/pcie-devices.json"
curl --silent --fail \
    "$base_url/redfish/v1/Chassis/GPU0/PCIeDevices/PCIeDevice0" \
    >"$output_dir/pcie-device0.json"

if [[ ! -f runtime/dbus-session.env ]]; then
    echo "The private D-Bus session address was not published." >&2
    exit 1
fi
# shellcheck disable=SC1091
source runtime/dbus-session.env
"$build_dir/tools/dbus_dump" >"$output_dir/dbus-objects.txt"

sensor_count="$(jq -r '."Members@odata.count"' "$output_dir/sensors.json")"
dbus_object_count="$(
    grep -c '^/xyz/openbmc_project' "$output_dir/dbus-objects.txt" || true
)"
if (( sensor_count < 11 )); then
    echo "Expected at least eleven Redfish sensors, found $sensor_count." >&2
    exit 1
fi
if (( dbus_object_count < 14 )); then
    echo "Expected at least fourteen D-Bus objects, found $dbus_object_count." >&2
    exit 1
fi

echo "Injecting a GPU0 temperature fault through Redfish and D-Bus..."
curl --silent --fail -X POST "$base_url/debug/faults" \
    -H "Content-Type: application/json" \
    -d '{"target":"GPU0_Core_Temp","fault":"out_of_range","enabled":true}' \
    >"$output_dir/fault-response.json"
sleep 2

curl --silent --fail \
    "$base_url/redfish/v1/Chassis/GPU0/Sensors/GPU0_Core_Temp" \
    >"$output_dir/sensor-after-fault.json"
curl --silent --fail \
    "$base_url/redfish/v1/Systems/System0/LogServices/EventLog/Entries" \
    >"$output_dir/events.json"

health="$(jq -r '.Status.Health' "$output_dir/sensor-after-fault.json")"
event_count="$(jq -r '."Members@odata.count"' "$output_dir/events.json")"
if [[ "$health" != "Critical" ]]; then
    echo "Expected Critical sensor health after fault, found $health." >&2
    exit 1
fi
if (( event_count < 1 )); then
    echo "Expected at least one EventLog entry after fault injection." >&2
    exit 1
fi

echo "Clearing the injected fault..."
curl --silent --fail -X POST "$base_url/debug/faults" \
    -H "Content-Type: application/json" \
    -d '{"target":"GPU0_Core_Temp","fault":"out_of_range","enabled":false}' \
    >"$output_dir/clear-response.json"

invalid_status="$(
    curl --silent -o "$output_dir/invalid-fault-response.json" \
        -w '%{http_code}' -X POST "$base_url/debug/faults" \
        -H "Content-Type: application/json" \
        -d '{"target":"missing","fault":"out_of_range","enabled":true}'
)"
if [[ "$invalid_status" != "400" ]]; then
    echo "Expected HTTP 400 for an unknown fault target, found $invalid_status." >&2
    exit 1
fi

echo "Smoke test passed: sensors=$sensor_count dbus_objects=$dbus_object_count health=$health events=$event_count"
