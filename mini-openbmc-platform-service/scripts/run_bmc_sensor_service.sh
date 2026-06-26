#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
mkdir -p runtime/logs runtime/sockets
exec ./build/services/bmc-sensor-service/bmc-sensor-service
