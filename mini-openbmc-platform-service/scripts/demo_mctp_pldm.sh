#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
mkdir -p runtime/logs runtime/sockets
rm -f runtime/sockets/mctp_endpoint.sock

cleanup() {
    kill "${agent_pid:-}" 2>/dev/null || true
    wait "${agent_pid:-}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

./build/services/pldm-endpoint-agent/pldm-endpoint-agent &
agent_pid=$!
for _ in $(seq 1 50); do
    [[ -S runtime/sockets/mctp_endpoint.sock ]] && break
    sleep 0.1
done
echo "Sending PLDM Type 0 GetTID over UDS-MCTP..."
./build/tools/mctp_ping
echo "The endpoint log contains PLDM Type 0 and Type 2 command metadata:"
tail -n 5 runtime/logs/mini-openbmc-service.jsonl
