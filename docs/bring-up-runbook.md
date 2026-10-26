# Bring-up runbook - QNX 8.0 guest on AGX Orin (KVM)

Goal: on AGX Orin R36.5, validate the dual-domain topology of "host Linux holds the GPU and runs TensorRT inference while a QNX 8.0 KVM guest coexists", and close the two known bring-up risks (GIC `Error(19)`, NISV hang) with measurements. Stage acceptance criteria in [docs/milestones.md](milestones.md); this file is the step-by-step sequence.

## Prerequisites (one-time)

| item | where | note |
|---|---|---|
| QNX SDP 8.0 (Linux host) | any x86_64 host | SDP 8.0.x, installs to ~/qnx800 by default; verify qcc/mkqnximage/mkifs/objdump available |
| Software Center: com.qnx.qnx800.target.qemuvirt | build host | stock startup-qemu-virt; without it mkqnximage fails the ifs step with "Host file not available" |
| Software Center: com.qnx.qnx800.bsp.hypervisor_guest_arm | build host | startup lib, used to rebuild the NISV-fixed startup in build-qemu-virt.sh |
| IFS artifacts | build host output/ | ifs.bin + disk-qemu.vmdk + disk-qemu (they come in pairs) |
| board reachable | `ssh <user>@<board-ip>` | USB / PCIe NIC both work; the bridged subnet is in [scripts/orin/setup-bridge.sh](../scripts/orin/setup-bridge.sh) |
| /dev/kvm | board | confirm first, see Step 1 |

## Step 1 - on-board KVM availability (5 min)

```bash
ssh <user>@<board-ip>
ls -la /dev/kvm || echo "NO /dev/kvm"
dmesg | grep -i kvm
# expect: kvm module loaded, /dev/kvm present (JetPack 6 L4T enables KVM)
```

## Step 2 - vGIC smoke test (first evening, about 15 min)

Purpose: separate the two failure modes before a full boot. The reference repo's docs/orin-port.md records an AGX-specific `Error(19)` (not reproducible on Orin Nano).

```bash
# minimal vGIC probe - no guest image needed
taskset -c 2,3 qemu-system-aarch64 -machine virt,gic-version=3 -cpu host \
  -enable-kvm -smp 2 -m 1G -nographic -serial mon:stdio \
  -kernel /dev/null 2>&1 | head -20
```

Expected outcomes:
- QEMU banner + a null-kernel "insufficient" style error, then **vGIC creation is fine**, the Error(19) risk is closed, proceed to Step 3
- `VmCreateGIC Error(19)` reported, then walk the degradation ladder: first rebuild the L4T kernel + the `tegra234-soc-minimal.dtsi` GICv3 patch (patch location in the reference repo's docs/orin-port.md), then try `gic-version=host` / drop the flag, finally TCG (functional level only; the timebase claims are void)

## Step 3 - full guest boot (NISV-fixed startup)

Sync the build host's artifacts (ifs.bin + vmdk + extent + SHA256SUMS) to the board, then:

```bash
sudo ./scripts/orin/setup-bridge.sh
./scripts/orin/launch-qnx-gpu-coexist.sh output/ifs.bin output/disk-qemu.vmdk
```

The expected serial output reaches `Startup complete`. If the SDP-shipped startup is used by mistake, the NISV hang appears right after `FOUND GICv3 ITS` - confirm the IFS was built with this repo's startup.

After the guest is up: `ifconfig` inside the guest shows 192.168.100.10; on the host, `ping 192.168.100.10`.

## Step 4 - GPU inference coexistence (30 min)

```bash
# host side: run the real 4-engine chain (engine build in models/README.md,
# the chain comes from the sibling repo sparsedrive-trt-edge), a ~10 Hz cadence loop
# board side: guest up + echo server; host: ping -c 18000 192.168.100.10
```

## Step 4 acceptance checklist (all must hold)

- [ ] guest reaches Startup complete + io-sock up + echo server listening
- [ ] host-side real 4-engine chain runs with valid output (tens of ms per frame)
- [ ] coexistence for 30 min: no OOM, no network jitter, latency drift explainable
- [ ] vCPU pinning (2,3) + the 1G frozen guest fixed in the launch script
- [ ] Error(19)/NISV dispositions archived; dmesg + boot log + bench JSON archived
- [ ] results kept on the private side per [results/README.md](../results/README.md)

## Known AGX vs Orin Nano differences (from the reference repo)

| item | Orin Nano (verified by the reference repo) | AGX Orin (what this repo validates) |
|---|---|---|
| GIC create | works | `Error(19)` recorded, test first in Step 2 |
| kernel | 5.15.148-tegra (R36.4.7) | 5.15.148-tegra (R36.5), same family |
| QEMU | 6.2.0 distro pkg | 6.2.0 via apt (Ubuntu 22.04 base) |
| debug UART | pins unknown | uarti (SBSA UART) on micro-USB |
| cores | 6x A78AE | 12x A78AE (more pinning headroom) |
| RAM | 8G (guest 1G) | 32/64G (guest 1G is on the small side) |

## Degradation ladder

1. kernel rebuild + the dtsi GICv3 patch
2. gic-version=host
3. TCG (functional level only; the timebase claims are void)
