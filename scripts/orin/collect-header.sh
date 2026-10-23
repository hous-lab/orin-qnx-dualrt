#!/usr/bin/env bash
# collect-header.sh — write the run header line for results/<ts>/.
# Records everything a rerun needs: SHAs, power, governor, kernel, QEMU.
set -euo pipefail
out="${1:-results/run-header.txt}"
mkdir -p "$(dirname "$out")"
{
echo "utc=$(date -u +%Y%m%dT%H%M%SZ)"
echo "kernel=$(uname -r)"
echo "qemu=$(qemu-system-aarch64 --version | head -1)"
echo "nvpmodel=$(nvpmodel -q 2>/dev/null | head -1 || true)"
echo "governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true)"
echo "ifs_sha256=$(sha256sum output/ifs.bin 2>/dev/null | cut -d' ' -f1 || true)"
echo "disk_sha256=$(sha256sum output/disk-qemu 2>/dev/null | cut -d' ' -f1 || true)"
} > "$out"
cat "$out"
