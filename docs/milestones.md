# Research milestones and acceptance criteria

Research proceeds in stages, each with written acceptance criteria (exit criteria); stage descriptions use technical content only. Detail docs: [docs/bring-up-runbook.md](bring-up-runbook.md) (bring-up), [docs/closed-loop-design.md](closed-loop-design.md) (closed loop), [docs/porting-diff.md](porting-diff.md) (dual backend), [docs/stability-postmortem-guide.md](stability-postmortem-guide.md) (stability).

## Stage 1 - feasibility: QNX guest coexisting with GPU inference

Run QNX 8.0 as a KVM guest on AGX Orin R36.5 while the host side runs a real TensorRT inference chain. Steps in [docs/bring-up-runbook.md](bring-up-runbook.md).

Acceptance: the guest boots to Startup complete (io-sock + echo server up); the host-side real 4-engine chain runs; the two coexist stably for 30 minutes; vCPU pinning and the frozen guest memory size are fixed in the launch scripts; the dispositions of the two known KVM risks (GIC Error(19) / NISV hang) are archived.

## Stage 2 - cross-domain IPC and timebase

Establish deterministic cross-VM IPC and a common timebase.

Acceptance: the 64B echo smoke; contract tests green on both toolchains (qcc/gcc); the timebase-calibration loop stable for 30 minutes; 64B/128B RTT baselines (3 repeats); frequency cross-check within 1 ppm, otherwise revise the "same oscillator" claim.

## Stage 3 - QNX scheduling runtime + real inference chain

A 6-thread fixed-priority QNX runtime (arithmetic release / watchdog / deadline monitor / safety state machine) drives the real SparseDrive 4-engine chain through the broker.

Acceptance: the runtime visible in pidin; T1/T2/T3 released at 10/30/10 Hz with at least 600 releases each, no gaps in the sequence; the first deadline-miss statistics attributable; a watchdog injection detected within one release period; the merged Perfetto timeline readable; the 5-segment view matching nsys NVTX.

Prerequisite: the QNX timer-resolution probe (1 ms / 100 ms steps) validated first.

## Stage 4 - interference experiments + unified timeline

Build the interference matrix and synthesize the cross-domain timeline.

Acceptance: at least 6 experiment arms x 3 repeats archived; the first Orin nsys export; a Perfetto timeline aligning the QNX trace with CUDA/NVTX; per-arm P50/95/99 + closure residual; per-arm mechanism explanation, not just curves. Hygiene rule: CSV numbers are always taken with nsys OFF; nsys is timeline evidence only.

## Stage 5 - ITaskRuntime dual backend

The same task table and telemetry schema implemented on both Linux and QNX.

Acceptance: the runtime.hpp interface + the Linux (timerfd+SCHED_FIFO) and QNX (TimerSettime+fixed priority) backends compile on both sides; the same task table loads on both; [docs/porting-diff.md](porting-diff.md) written (per-difference + reason, including the priority direction inversion).

## Stage 6 - event-triggered workloads

QNX state-machine event, then a one-shot triggered task, then a broker burst, then asynchronous completion; the state machine gains the TRIGGERED path, coexisting with the resident chain under the same contract.

Acceptance: the triggered task reaches the broker after the state-machine event; the burst duration matches the trigger config; COMPLETE flows back asynchronously; the resident chain is undisturbed.

## Stage 7 - online closed-loop multi-model scheduling

Plug the QNX scheduling authority into a real CARLA online closed loop: the SparseDrive main chain plus several official Autoware models coexist, all inference driven by QNX releases. Design and conclusions in [docs/closed-loop-design.md](closed-loop-design.md).

Acceptance: the three official models' TRT engines build and infer individually; after the release-gate injection the main chain is driven at QNX cadence; the CARLA camera stream closes the loop drivable under the gated chain, with multi-model contention measured.

## Stage 8 - stability closure

Long-horizon continuous operation answers "how long can the system stay stable, and where do failures come from".

Acceptance: multi-day continuous QNX runtime operation with zero QNX-side faults (concluded by deliberate shutdown); all interruptions attributed to the external simulation/bridge side; the death-cause triage and re-run procedure written up ([docs/stability-postmortem-guide.md](stability-postmortem-guide.md)). Quantitative numbers stay private per NCEULA ([results/README.md](../results/README.md)).
