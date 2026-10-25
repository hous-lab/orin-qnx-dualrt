# NO-GO：原生 QNX Hypervisor 路线（Tegra234）

本文是项目的负面结果存档：一条已关闭的技术路线与关闭依据。当前仓库采用的是 KVM guest 路线，见 [docs/bring-up-runbook.md](bring-up-runbook.md)。

## 路线

量产平台（DRIVE OS）的 Linux/QNX 双域拓扑由认证 hypervisor 承载。本研究的首选路线原本与量产同构：在 Tegra234（AGX Orin）上以原生 QNX Hypervisor 直接承载 Linux + QNX 双 guest。bring-up 阶段对这条路线做了可行性验证，结论为不可行，路线关闭。

## 关闭依据

三条平台事实，均在 bring-up 阶段直接确认：

- FACT 1 — Tegra234 的 iGPU 没有可用的 SMMU stream ID：hypervisor 无法做 DMA 隔离，无法把 GPU 资源安全地赋给 QNX guest，GPU 直通不成立。
- FACT 2 — BPMP 时钟/电源管理接口没有 QNX client：GPU/EMC 等时钟与功耗控制经由 BPMP firmware，平台侧只提供 Linux client，QNX 侧无法自行管理 GPU 时钟域。
- FACT 3 — A78AE 内核不支持嵌套 KVM：以 QNX 为宿主、Linux KVM 叠加其上的形态不成立，反之亦然。

## Stop condition（路线关闭的判定）

任一必要条件缺失即关闭该路线。iGPU stream ID 缺失单独即已封死 GPU 直通；BPMP 无 QNX client 与 A78AE 不支持嵌套 KVM 使剩余形态也不成立。该结论绑定 Tegra234 + 当时的 L4T/BPMP 固件状态；未来平台固件/内核更新是否改变这些事实，未经验证（HYPOTHESIS，不作为事实口吻）。KVM guest 路线的边界（hypervisor 为宿主中介、QNX guest 无 GPU 访问）见 [README](../README.md) 的 What this is NOT（诚实边界）段。
