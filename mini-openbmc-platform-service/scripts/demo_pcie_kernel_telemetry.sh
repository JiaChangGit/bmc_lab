#!/usr/bin/env bash
set -euo pipefail

sysfs="/sys/class/mini_bmc_pcie/mini_pcie0"
if [[ ! -d "$sysfs" || ! -c /dev/mini_pcie0 ]]; then
    echo "mini_pcie_telemetry is not loaded." >&2
    exit 1
fi

echo "Current PCIe telemetry:"
cat /dev/mini_pcie0
for attribute in device_id link_width link_speed link_state \
    gpu_core_temp_millic gpu_power_milliwatt \
    correctable_error_count nonfatal_error_count health fault_mode; do
    printf '%s=' "$attribute"
    cat "$sysfs/$attribute"
done

echo "Injecting link_degraded..."
echo link_degraded | sudo tee "$sysfs/fault_mode" >/dev/null
cat /dev/mini_pcie0
echo "Clearing fault..."
echo none | sudo tee "$sysfs/fault_mode" >/dev/null
