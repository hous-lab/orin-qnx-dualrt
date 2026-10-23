#!/usr/bin/env bash
# launch-qnx-gpu-coexist.sh — start the QNX guest beside GPU inference on AGX Orin.
#
# Differences from the reference launcher (scripts/orin/reference/):
#   1. vCPU pinning: QEMU threads -> cores 2,3 (0,1 left to host/IRQ;
#      broker cores 4..11 asserted free)
#   2. -snapshot mandatory (disk otherwise drifts across runs)
#   3. slot order blk -> net preserved (io-sock requirement)
#   4. AGX memory: guest 1G of 32/64G; A0/A1 arms quantify the overhead.
#
# Run on the AGX, after setup-bridge.sh + artifacts synced:
#   ./launch-qnx-gpu-coexist.sh [ifs.bin] [disk-qemu.vmdk]
set -euo pipefail

IFS_BIN="${1:-output/ifs.bin}"
DISK="${2:-output/disk-qemu.vmdk}"

[ -f "$IFS_BIN" ] || { echo "ERROR: $IFS_BIN not found" >&2; exit 1; }
[ -f "$DISK" ] || { echo "ERROR: $DISK not found" >&2; exit 1; }
[ -f "${DISK%.vmdk}" ] || { echo "ERROR: raw extent ${DISK%.vmdk} missing (descriptor needs it)" >&2; exit 1; }
[ -e /dev/kvm ] || { echo "ERROR: /dev/kvm missing" >&2; exit 1; }
ip link show tap-qnx >/dev/null 2>&1 || { echo "ERROR: tap-qnx missing — run setup-bridge.sh" >&2; exit 1; }

echo "Host kernel: $(uname -r)"
nproc_avail=$(nproc)
echo "cores: $nproc_avail (pinning QEMU vCPU threads to 2,3)"

# sanity: IFS sha must match the ledger synced from the build host
if [ -f output/SHA256SUMS ]; then
    ( cd output && sha256sum -c SHA256SUMS --quiet ) \
        || { echo "ERROR: SHA256SUMS mismatch" >&2; exit 1; }
fi

echo "Launching QNX guest (Ctrl-A X quit, Ctrl-A C QEMU monitor) ..."
exec taskset -c 2,3 qemu-system-aarch64 \
  -machine virt,gic-version=3 \
  -cpu host \
  -enable-kvm \
  -smp 2 \
  -m 1G \
  -snapshot \
  -drive file="${DISK}",if=none,id=drv0 \
  -device virtio-blk-device,drive=drv0 \
  -netdev tap,id=n0,ifname=tap-qnx,script=no,downscript=no \
  -device virtio-net-device,netdev=n0,mac=52:54:00:11:11:11 \
  -device virtio-rng-device \
  -kernel "${IFS_BIN}" \
  -nographic \
  -serial mon:stdio
