#!/usr/bin/env bash
set -euo pipefail

hwmon=""
for name_file in /sys/class/hwmon/hwmon*/name; do
    if [[ -f "$name_file" ]] && [[ "$(<"$name_file")" == "mini_i2c_hwmon" ]]; then
        hwmon="$(dirname "$name_file")"
        break
    fi
done
if [[ -z "$hwmon" ]]; then
    echo "mini_i2c_hwmon is not loaded." >&2
    exit 1
fi

echo "Using $hwmon"
for attribute in name temp1_label temp1_input in1_label in1_input \
    fan1_label fan1_input fault_mode; do
    printf '%s=' "$attribute"
    cat "$hwmon/$attribute"
done
echo "Injecting out_of_range..."
echo out_of_range | sudo tee "$hwmon/fault_mode" >/dev/null
cat "$hwmon/temp1_input" "$hwmon/in1_input" "$hwmon/fan1_input"
echo none | sudo tee "$hwmon/fault_mode" >/dev/null
