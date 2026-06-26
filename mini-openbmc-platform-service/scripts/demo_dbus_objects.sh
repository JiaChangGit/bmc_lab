#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
build_dir="${MINI_BMC_BUILD_DIR:-build}"

if [[ -f runtime/dbus-session.env ]]; then
    # shellcheck disable=SC1091
    source runtime/dbus-session.env
fi
if [[ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]]; then
    echo "No active MiniBMC D-Bus session was found." >&2
    echo "Run scripts/run_session.sh in another terminal first." >&2
    exit 1
fi

echo "MiniBMC D-Bus object properties:"
"$build_dir/tools/dbus_dump"
echo "MiniBMC D-Bus object tree:"
busctl --address="$DBUS_SESSION_BUS_ADDRESS" tree \
    xyz.openbmc_project.MiniBMC.SensorService
