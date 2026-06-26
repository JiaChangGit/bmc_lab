#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if lsmod | awk '{print $1}' | grep -Eq '^(mini_i2c_hwmon|mini_pcie_telemetry)$'; then
    if sudo -n true 2>/dev/null; then
        ./scripts/unload_kernel_modules.sh
    else
        echo "Kernel modules are loaded but passwordless sudo is unavailable; skipping unload."
    fi
fi

rm -rf build runtime
if [[ -d /lib/modules/"$(uname -r)"/build ]]; then
    make -C kernel/mini_pcie_telemetry clean >/dev/null
    make -C kernel/mini_i2c_hwmon clean >/dev/null
fi
find kernel -type f \( \
    -name '*.o' -o -name '*.ko' -o -name '*.mod' -o -name '*.mod.c' -o \
    -name '*.order' -o -name '*.symvers' -o -name '.*.cmd' -o \
    -name '*.dwo' \
    \) -delete
find kernel -type d -name '.tmp_versions' -prune -exec rm -rf {} +
echo "Build and runtime artifacts removed."
