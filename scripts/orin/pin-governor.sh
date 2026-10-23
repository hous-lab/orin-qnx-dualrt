#!/usr/bin/env bash
# pin-governor.sh — fix CPU governor + record power mode (run as root).
# Every bench arm pins the same; header records what was set.
set -euo pipefail
gov="${1:-performance}"
for c in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo "$gov" > "$c" 2>/dev/null || echo "  skip $c"
done
echo "governor=$gov"
nvpmodel -q 2>/dev/null || echo "(nvpmodel not readable here)"
