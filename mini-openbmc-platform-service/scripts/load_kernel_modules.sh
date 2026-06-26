#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
running_kernel="$(uname -r)"

load_module() {
    local module="$1"
    local path="$2"
    local module_kernel
    if lsmod | awk '{print $1}' | grep -qx "$module"; then
        echo "$module is already loaded; skipping."
        return
    fi
    if [[ ! -f "$path" ]]; then
        echo "Module file is missing: $path" >&2
        echo "Run scripts/build_kernel_modules.sh first." >&2
        exit 1
    fi
    module_kernel="$(modinfo -F vermagic "$path" 2>/dev/null | awk '{print $1}')"
    if [[ -z "$module_kernel" ]]; then
        echo "Unable to read module vermagic: $path" >&2
        exit 1
    fi
    if [[ "$module_kernel" != "$running_kernel" ]]; then
        echo "Module $module was built for $module_kernel, not $running_kernel." >&2
        echo "Install matching kernel headers and rebuild before loading." >&2
        exit 1
    fi
    echo "Loading $module..."
    sudo insmod "$path"
}

load_module mini_pcie_telemetry \
    "$root/kernel/mini_pcie_telemetry/mini_pcie_telemetry.ko"
load_module mini_i2c_hwmon \
    "$root/kernel/mini_i2c_hwmon/mini_i2c_hwmon.ko"
echo "Kernel modules loaded."
