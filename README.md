# orin-qnx-dualrt

DualOS inference runtime research on AGX Orin: a Linux domain holds the GPU and runs real TensorRT inference (the SparseDrive 4-engine chain plus several official Autoware ONNX models), while QNX 8.0 runs as a KVM guest providing fixed-priority release scheduling and supervision (release scheduler, watchdog, deadline monitor, safety state machine). The domains are connected by a 128B contract-frame IPC with 9 timestamps, producing a unified cross-domain latency timeline and interference experiments.

> **Status: Experimental - research concluded** - results last verified on AGX Orin R36.5 + QNX SDP 8.0 + TensorRT 10.x; the x86 contract tests run on any machine. Research code, no long-term compatibility promises; [issues](https://github.com/hous-lab/orin-qnx-dualrt/issues) are welcome, but please open an issue before submitting PRs (on-board re-testing is gated by SDP/hardware access).

## Why QNX on Orin (research positioning)

The core value of a hard real-time OS (such as QNX) in production automotive systems is **deterministic scheduling**: an independent safety domain owns the authority over when each task is released, while the compute domain only executes. This turns "did inference finish on time" into a supervisable, attributable, re-testable question, which is what protects system-level stability.

Production platforms (NVIDIA DRIVE) close the Linux/QNX dual-domain topology inside a proprietary platform. The research question of this project: **in an open research environment (AGX Orin + KVM + your own QNX SDP 8.0), can this dual-domain topology be reproduced and validated with real inference workloads?**

## Main work
> Per-stage tasks and acceptance criteria: [docs/milestones.md](docs/milestones.md)

- **QNX guest on the board (feasibility)** - a QNX 8.0 guest boots on AGX Orin R36.5 under KVM and coexists with GPU inference long-term; the SDP-shipped startup has a GICv3 NISV defect under KVM, so we built our own startup board (`-fno-auto-inc-dec` fix + a disassembly gate check) and ran IFS identity acceptance (dumpifs size fingerprint)
- **Cross-domain IPC and timebase** - 64B echo smoke, then the 128B frame_v2 contract (13 contract tests green on both toolchains), then the cross-VM timebase calibration loop, then an RTT baseline
- **QNX scheduling runtime + real inference chain** - a 6-thread fixed-priority QNX runtime (arithmetic release / watchdog / deadline monitor / safety state machine); a Linux broker drives the real SparseDrive 4-engine chain (zero-copy wiring via setTensorAddress); deadline-miss statistics and the 7-segment latency breakdown
- **Interference experiments + unified timeline** - Orin nsys full kernel trace + QNX trace merged into a unified Perfetto timeline; an interference matrix (GPU duty cycle / CPU core count / task set / power mode) with P50/95/99 distributions, each experiment arm with a mechanism explanation
- **ITaskRuntime dual backend** - the same task table loads on Linux (timerfd) and QNX (timer+sigwaitinfo) backends; six measured porting differences quantified (priority direction inversion, TIMER_ABSTIME flag face, standard-signal multiplexing trap, and more) - [docs/porting-diff.md](docs/porting-diff.md)
- **Event-triggered workloads** - QNX state-machine event, then a one-shot triggered task, then a broker burst, coexisting with the resident chain under the same contract
- **Online closed-loop multi-model scheduling** - a ~90-line release-gate injected into an unmodified ROS 2 node; three official Autoware models plus the SparseDrive main chain all driven by QNX releases; real CARLA camera frames processed by the gated chain at QNX cadence
- **Stability closure** - multi-day continuous QNX runtime operation with zero QNX-side faults, concluded by deliberate shutdown; death-cause triage and re-run procedure: [docs/stability-postmortem-guide.md](docs/stability-postmortem-guide.md)


## What we demonstrated (qualitative)

- Long-term coexistence of a QNX guest with real GPU inference (not minute-level smoke: it spans reboots and interference events)
- Real closed loop: CARLA camera, then QNX gate, then the real 4-engine chain, then control, then CARLA - the vehicle cruises smoothly under scheduled inference and self-recovers from stalls
- Multi-model coexistence scheduling: 3 official Autoware models alongside the main chain, zero misses in the acceptance window (qualitative; exact numbers stay private per NCEULA - re-test via [bench/](bench/))
- Multi-day continuous QNX runtime operation with zero QNX-side faults (concluded by deliberate shutdown, qualitative; numbers stay private)
- release-gate injected into an unmodified ROS 2 node (zero node-logic changes; scheduling authority stays in QNX)
- ITaskRuntime dual backend (Linux timerfd / QNX timer+sigwaitinfo) loading the same task table, zero misses in acceptance windows on both sides, porting differences documented
- The stability ceiling is set by the simulation-environment side, not by QNX - [docs/stability-postmortem-guide.md](docs/stability-postmortem-guide.md)

## What this is NOT (honest boundaries)

- Not DRIVE OS and not a reproduction of it. The boundary is KVM (host-mediated), not a certified Type-1 hypervisor; the QNX guest has no GPU access
- No real-time guarantees, determinism, freedom-from-interference, or ASIL / ISO 26262 claims
- The native QNX Hypervisor route is infeasible on Tegra234 (iGPU has no SMMU stream ID, BPMP has no QNX client, A78AE has no nested KVM); out of project scope ([docs/no-go-native-qnx-hypervisor.md](docs/no-go-native-qnx-hypervisor.md))
- The closed loop uses the CARLA simulator, not a real vehicle; except T1, no model output enters the control path (they are real-envelope competing workloads)
- **QNX NCEULA constraint**: QNX SDP 8.0 is used under the BlackBerry QNX Non-Commercial Use EULA; evaluation results (benchmark numbers) of QNX software must not be published without written approval - this repository publishes methodology and code only, and quantitative results are not committed (enforced by .gitignore); QNX/BSP-derived binaries (startup, ifs.bin, disk images) are not redistributable, every user builds with their own SDP 8.0; users must obtain and comply with their own QNX SDP license

## Repo layout

| Directory | Contents |
|---|---|
| `contract/` | single source of truth for the wire contract (128B frame_v2), shared by both sides |
| `qnx/` | QNX side: runtime (scheduling/supervision) / trace / timebase / smoke / IFS build |
| `qnx/ifs/` | startup-qemu-virt source (with the NISV fix and the gate check) + IFS build recipe |
| `qnx/build/` | local mkqnximage/IFS build config + credential generation - [misc_files/README.md](qnx/build/local/misc_files/README.md) |
| `linux/` | Linux side: broker / workloads / IPC / reference-client |
| `timeline/` | nsys profile + QNX trace, merged into a unified Perfetto timeline |
| `bench/` | experiment-arm definitions / runner / statistics (P50/95/99, closure residual, stability monitoring) |
| `models/` | model download table (link-only): sparsedrive-trt-edge + official Autoware ONNX |
| `results/` | run artifacts are not committed (policy: [results/README.md](results/README.md)) |
| `itask/` | ITaskRuntime Linux/QNX dual-backend abstraction |
| `scripts/orin/` | bridge / launch (vCPU pinning) / governor / collection scripts |
| `scripts/x86/` | closed-loop demo launcher (env-var-driven template) |
| `g6_gate/` | release-gate header (for injecting into ROS 2 nodes) |
| `docs/` | bring-up runbook, contract doc, closed-loop scheduling design, porting diffs, stability procedure, milestones and acceptance |
| `PROVENANCE.md` | per-file provenance + license inventory |

## Get Started

The repo splits into an x86-verifiable part and an on-board part. The x86 part runs on any machine; the on-board part needs an AGX Orin (JetPack R36.5) and your own QNX SDP 8.0 (Non-Commercial Use EULA).

1. **Contract tests (x86, no dependencies)**

   ```bash
   cd contract && make && ./test_contract
   ```
   13 contract tests green means both toolchains agree.

2. **Build the QNX guest image (SDP host)**

   ```bash
   # requires your own QNX SDP 8.0 + BSP (build-ifs.sh has fail-fast gates)
   export BSP=<BSP_hyp-guest-arm_be-800_* install dir>
   bash qnx/build/build-ifs.sh
   # generate guest credentials (not committed): bash qnx/build/local/misc_files/gen_guest_keys.sh
   ```
   The resulting ifs.bin/disk images are **not distributed** - local use only.

3. **On-board bring-up** (AGX Orin, JetPack R36.5): see [docs/bring-up-runbook.md](docs/bring-up-runbook.md) (bring-up checkpoints, bridge/vCPU pinning/governor setup); the runtime-side task table is in qnx/stage/tasks*.csv.

4. **Run the real chain + multi-model scheduling** (optional, closed-loop demo): engine/ONNX download and build in [models/README.md](models/README.md); broker_chain takes explicit parameters (see its usage); the x86 closed-loop stack uses [scripts/x86/run_local_g6.sh](scripts/x86/run_local_g6.sh) (env-var template, depends on [sparsedrive-trt-edge](https://github.com/hous-lab/sparsedrive-trt-edge) step5/step6 + CARLA). Stability monitoring and death-cause attribution: [docs/stability-postmortem-guide.md](docs/stability-postmortem-guide.md).

5. **Measurement** ([bench/](bench/)): the interference matrix ([bench/stats/g3_analyze.py](bench/stats/g3_analyze.py)) + the cross-domain Perfetto timeline ([timeline/merge/to_perfetto.py](timeline/merge/to_perfetto.py)). Re-test to get your own numbers.

## Hardware & software

| Item | Value |
|---|---|
| Board | NVIDIA AGX Orin (Tegra234), JetPack/L4T R36.5, MAXN |
| Guest | QNX SDP 8.0 (Non-Commercial Use EULA), KVM guest (2 vCPU / 1G frozen) |
| Linux side | TRT 10.x (fp16 engines, built on-board), CARLA 0.9.15 (optional closed loop) |
| Portability | Engines/images are environment-derived and non-transferable; code and contract are portable. Numbers depend strongly on the exact TRT version; no transferability is guaranteed |

## Acknowledgements

- [ChHaEoN/qnx-linux-dual-vm-proxy](https://github.com/ChHaEoN/qnx-linux-dual-vm-proxy) (MIT; build-qemu-virt.sh is Apache-2.0)
- [sparsedrive-trt-edge](https://github.com/hous-lab/sparsedrive-trt-edge) - SparseDrive main-chain ONNX export and deployment pipeline (sibling repo)
- [Autoware](https://github.com/autowarefoundation/autoware_universe)
- [SparseDriveV2](https://github.com/swc-17/SparseDriveV2) (MIT)

## License

- MIT (see LICENSE)
- Third-party sources and licenses: [PROVENANCE.md](PROVENANCE.md)
- QNX SDP 8.0 constraints (non-commercial use, evaluation-publication limits, non-redistributable derived binaries): see the NCEULA section above and the boundary statement in [PROVENANCE.md](PROVENANCE.md)
- This repository is research-only, for non-commercial purposes
