#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
running_kernel="$(uname -r)"
kernel_build="${KDIR:-/lib/modules/$running_kernel/build}"
compile_only=false

if [[ ! -d "$kernel_build" ]]; then
    if [[ -n "${KDIR:-}" ]]; then
        echo "Kernel build directory is missing: $kernel_build" >&2
        exit 1
    fi
    kernel_build="$(
        find /usr/src -maxdepth 1 -type d -name 'linux-headers-*-generic' \
            -print 2>/dev/null | sort -V | tail -1
    )"
    if [[ -z "$kernel_build" || ! -f "$kernel_build/Makefile" ]]; then
        echo "Kernel headers are missing for $running_kernel." >&2
        echo "Install linux-headers-$running_kernel, or set KDIR to a valid headers tree." >&2
        exit 1
    fi
    compile_only=true
    echo "Matching headers for $running_kernel are unavailable."
    echo "Using $kernel_build for compile-only validation."
fi

echo "Building mini_pcie_telemetry..."
make -C "$root/kernel/mini_pcie_telemetry" KDIR="$kernel_build"
echo "Building mini_i2c_hwmon..."
make -C "$root/kernel/mini_i2c_hwmon" KDIR="$kernel_build"
if [[ "$compile_only" == true ]]; then
    echo "Warning: built modules do not match the running kernel and must not be loaded."
fi
echo "Kernel module build completed."
