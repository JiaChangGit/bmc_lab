#!/usr/bin/env bash
set -euo pipefail

unload_module() {
    local module="$1"
    if ! lsmod | awk '{print $1}' | grep -qx "$module"; then
        echo "$module is not loaded; skipping."
        return
    fi
    echo "Unloading $module..."
    sudo rmmod "$module"
}

unload_module mini_i2c_hwmon
unload_module mini_pcie_telemetry
echo "Kernel module unload completed."
