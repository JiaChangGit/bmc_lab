#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
build_dir="${MINI_BMC_BUILD_DIR:-build}"

for binary in \
    "$build_dir/services/pldm-endpoint-agent/pldm-endpoint-agent" \
    "$build_dir/services/bmc-sensor-service/bmc-sensor-service" \
    "$build_dir/services/redfish-service/redfish-service"; do
    if [[ ! -x "$binary" ]]; then
        echo "Missing binary: $binary" >&2
        echo "Run scripts/build.sh first." >&2
        exit 1
    fi
done

mkdir -p runtime/logs runtime/sockets
rm -f runtime/sockets/mctp_endpoint.sock

echo "Starting services in a private D-Bus session..."
exec dbus-run-session -- bash -c '
set -euo pipefail
root="$1"
build_dir="$2"
cd "$root"

cleanup() {
    trap - EXIT INT TERM
    rm -f runtime/dbus-session.env
    for pid in "${redfish_pid:-}" "${sensor_pid:-}" "${pldm_pid:-}"; do
        if [[ -n "$pid" ]]; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    wait 2>/dev/null || true
}
trap cleanup EXIT INT TERM

printf "export DBUS_SESSION_BUS_ADDRESS=%q\n" "$DBUS_SESSION_BUS_ADDRESS" \
    > runtime/dbus-session.env

"$build_dir/services/pldm-endpoint-agent/pldm-endpoint-agent" &
pldm_pid=$!
for _ in $(seq 1 50); do
    [[ -S runtime/sockets/mctp_endpoint.sock ]] && break
    sleep 0.1
done
if [[ ! -S runtime/sockets/mctp_endpoint.sock ]]; then
    echo "PLDM endpoint socket did not appear." >&2
    exit 1
fi

"$build_dir/services/bmc-sensor-service/bmc-sensor-service" &
sensor_pid=$!
sleep 0.5
"$build_dir/services/redfish-service/redfish-service" &
redfish_pid=$!

ready=false
for _ in $(seq 1 50); do
    if curl --silent --fail \
        http://127.0.0.1:8080/redfish/v1/Chassis/GPU0/Sensors >/dev/null; then
        echo "Demo is ready at http://127.0.0.1:8080/redfish/v1"
        ready=true
        break
    fi
    sleep 0.1
done
if [[ "$ready" != true ]]; then
    echo "Redfish service did not become ready with D-Bus sensor data." >&2
    exit 1
fi

wait "$redfish_pid"
' bash "$root" "$build_dir"
