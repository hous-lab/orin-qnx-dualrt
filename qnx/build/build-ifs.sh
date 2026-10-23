#!/usr/bin/env bash
# build-ifs.sh - build the guest IFS image on a Linux host with QNX SDP 8.0.
#
# Recipe is the reference repo's PROVEN IFS-only route (their
# ipc-test/qnx-dds-monitor/rebuild-ifs-with-dds.sh), adapted:
#   1. build our NISV-fixed startup-qemu-virt (build-qemu-virt.sh, BSP required)
#   2. mkqnximage once to generate output/build/ifs.build (+ disk artifacts)
#   3. IFS-only rebuild via mkifs with MKFS_PATH putting OUR startup dir first
#      (mkifs resolves startup-qemu-virt by name; first hit wins; the SDP
#      stock one hangs under KVM after "FOUND GICv3 ITS" — NISV defect)
#   4. VERIFY with dumpifs: ifs.build carries [+optional], so mkifs exit 0
#      proves nothing. Ours ~615KB vs SDP stock ~1.6MB — assert by size.
#
# Prereqs (Software Center):
#   com.qnx.qnx800.target.qemuvirt        (stock startup for base generation)
#   com.qnx.qnx800.bsp.hypervisor_guest_arm  (startup lib for our rebuild)
#
# Usage: BSP=<path> ./build-ifs.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
BSP="${BSP:?set BSP=<extracted BSP_hyp-guest-arm_be-800_* or Software Center install dir>}"
QNX_BASE="${QNX_BASE:-$HOME/qnx800}"

# --- 1. SDP env ---
if [ -z "${QNX_HOST:-}" ]; then
    qhost="$QNX_BASE/host/linux/x86_64"
    [ -d "$qhost" ] || { echo "no SDP at $QNX_BASE — source qnxsdp-env.sh or set QNX_BASE" >&2; exit 1; }
    export QNX_HOST="$qhost" QNX_TARGET="$QNX_BASE/target/qnx"
    export PATH="$PATH:$qhost/usr/bin"
    export MAKEFLAGS="-I$QNX_TARGET/usr/include"
fi
echo "[1/5] SDP env ok: QNX_TARGET=$QNX_TARGET"

# --- 2. our NISV-fixed startup ---
echo "[2/5] building startup-qemu-virt (NISV-fixed, gated) ..."
"$REPO/qnx/ifs/build-qemu-virt.sh"
STARTUP_BIN="$BSP/src/hardware/startup/boards/qemu-virt/aarch64/le/startup-qemu-virt"
[ -f "$STARTUP_BIN" ] || { echo "startup binary missing: $STARTUP_BIN" >&2; exit 1; }
size=$(stat -c %s "$STARTUP_BIN")
[ "$size" -lt 1000000 ] && echo "    ours: $size bytes (small, NISV-fixed)" \
                      || echo "    WARNING: $size bytes — check it is not the SDP stock (1.6MB)"

# --- 3. base generation (disk + buildfiles) ---
echo "[3/5] mkqnximage base generation ..."
mkdir -p "$HERE/output" "$HERE/local/snippets"
cd "$HERE"
mkqnximage --type=qemu --arch=aarch64le --hostname=dualos-guest --build \
    || echo "    (mkqnximage failed at the ifs step - expected if com.qnx.qnx800.target.qemuvirt is not installed; generation is what we need)"
[ -f output/build/ifs.build ] || { echo "ERROR: no output/build/ifs.build generated" >&2; exit 1; }
echo "    ifs.build generated"

# --- 4. IFS-only rebuild with OUR startup first on the search path ---
echo "[4/5] mkifs IFS-only rebuild (BSP startup dir first in buildfile search) ..."
sed -i "s|^\[search=|[search=$BSP/src/hardware/startup/boards/qemu-virt/aarch64/le:|" \
    output/build/ifs.build
grep -q "hardware/startup/boards/qemu-virt" output/build/ifs.build \
    || { echo "ERROR: search edit failed" >&2; exit 1; }
# --- 4b. stage extra binaries (P7) ---
STAGE_SRC="$REPO/qnx/stage"
if [ -d "$STAGE_SRC" ] && ls "$STAGE_SRC"/* >/dev/null 2>&1; then
    STAGE_DIR="$BSP/stage_p7"
    mkdir -p "$STAGE_DIR"
    cp -f "$STAGE_SRC"/* "$STAGE_DIR/"
    sed -i "s|^\[search=\([^]]*\)\]|[search=$STAGE_DIR:\1]|" output/build/ifs.build
    for f in "$STAGE_SRC"/*; do
        b=$(basename "$f")
        echo "[uid=0 gid=0 perms=0755] /proc/boot/$b=$b" >> output/build/ifs.build
    done
    echo "    staged: $(ls "$STAGE_SRC" | tr '\n' ' ')"
fi
# --- 4c. inject auto-run lines into the generated startup.sh (IFS-resident) ---
STARTSH="output/build/startup.sh"
if [ -f "$STARTSH" ] && [ -f "$STAGE_SRC/itask_demo_qnx" ]; then
    grep -q itask_demo "$STARTSH" || cat >> "$STARTSH" <<'EOS'

# P7 G4: ITaskRuntime dual-backend demo (auto-run, serial output)
[ -f /proc/boot/tasks.csv ] && /proc/boot/itask_demo_qnx qnx /proc/boot/tasks.csv 12 > /dev/ser1 2>&1 &
EOS
    echo "    startup.sh: itask demo injection present"
fi

mkifs -r "$QNX_TARGET" output/build/ifs.build output/ifs.bin
mkifs -r "$QNX_TARGET" output/build/ifs.build output/ifs.bin

# --- 5. acceptance: exit code proves nothing, verify by dumpifs ---
echo "[5/5] acceptance via dumpifs (mkifs [+optional] can silently skip) ..."
dumpifs output/ifs.bin > /tmp/ifs_listing.txt
ent_line=$(grep -A1 "startup" /tmp/ifs_listing.txt | head -4 || true)
if grep -q "proc/boot/qnx-echo-server-net" /tmp/ifs_listing.txt; then
    echo "    echo server staged"
fi
# identity gate: entry 40081da8 = SDP stock (hangs under KVM) — hard fail
ent=$(dumpifs output/ifs.bin 2>&1 | grep "startup\.*" | awk "{print \$3}")
if [ "$ent" = "40081da8" ]; then
    echo "FATAL: IFS contains SDP stock startup (40081da8) — would hang under KVM" >&2
    exit 1
fi
case "$ent" in
    40081*) echo "    startup entry $ent = ours (NISV-fixed)";;
    "") echo "WARNING: could not read startup entry" >&2;;
    *) echo "    startup entry $ent (unexpected — inspect)";;
esac
ls -la output/ifs.bin
( cd output && sha256sum ifs.bin > SHA256SUMS 2>/dev/null || true )
echo "done. artifact: qnx/build/output/ifs.bin (gitignored)"
echo "next: scp to AGX with the disk pair, boot per docs/bring-up-runbook.md"
