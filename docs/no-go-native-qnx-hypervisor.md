# NO-GO: the native QNX Hypervisor route (Tegra234)

This file is the project's negative-result archive: a technical route that was closed, with the evidence that closed it. The repository follows the KVM guest route, see [docs/bring-up-runbook.md](bring-up-runbook.md).

## The route

Production platforms (DRIVE OS) host the Linux/QNX dual-domain topology on a certified hypervisor. The preferred route of this research was originally isomorphic with production: on Tegra234 (AGX Orin), host Linux + QNX dual guests directly on the native QNX Hypervisor. Bring-up stage validated this route's feasibility and the conclusion was infeasible, route closed.

## Closure evidence

Three platform facts, all confirmed directly during bring-up:

- FACT 1 - the Tegra234 iGPU has no usable SMMU stream ID: the hypervisor cannot do DMA isolation and cannot safely assign GPU resources to the QNX guest, so GPU passthrough does not stand.
- FACT 2 - the BPMP clock/power-management interface has no QNX client: GPU/EMC clock and power control go through BPMP firmware, the platform provides a Linux client only, so the QNX side cannot manage the GPU clock domains itself.
- FACT 3 - the A78AE cores do not support nested KVM: the stacked form with QNX as host does not stand, and vice versa.

## Stop condition (how the route was judged closed)

Any missing necessary condition closes the route. The missing iGPU stream ID alone sealed GPU passthrough; the BPMP no-QNX-client and no-nested-KVM facts close the remaining forms. The conclusion is bound to Tegra234 + the then-current L4T/BPMP firmware state; whether future platform firmware/kernel updates change these facts is unverified (HYPOTHESIS, never written in a factual voice). The boundary of the KVM guest route (hypervisor as host intermediary, QNX guest with no GPU access) is in the What this is NOT section of [README](../README.md).
