# orin-qnx-dualrt

AGX Orin 上的双域（DualOS）推理运行时研究：Linux 域持有 GPU 跑真实 TensorRT 推理（SparseDrive 4-engine 链 + 多枚官方 Autoware onnx 模型），QNX 8.0 以 KVM guest 形态运行固定优先级调度/监督（release scheduler、watchdog、deadline monitor、安全状态机），跨域以 128B 契约帧 + 9 时间戳 IPC 连接，输出统一的跨域延迟时间线与干扰实验。

## Why: 为什么在 Orin 上做 QNX（研究定位）

硬实时 OS（如 QNX）在量产车载系统中的核心价值是**确定性调度**：由一个独立的 safety 域掌握"何时释放哪个任务"的权威，compute 域只负责执行，从而把"推理是否按时完成"变成可监督、可归因、可复测的问题，保障系统级稳定性。

量产平台（NVIDIA DRIVE）把 Linux/QNX 双域拓扑封闭在专有平台里。
本项目的研究问题：**在开放的研究环境（AGX Orin + KVM + 自备 QNX SDP 8.0）下，能否复刻这一双域拓扑，并用真实推理负载进行验证**。

## 主要工作
> 每阶段任务与验收： [docs/milestones.md](docs/milestones.md)

- **QNX guest 上板（可行性）** — QNX 8.0 guest 在 AGX Orin R36.5 的 KVM 上启动并与 GPU 推理长期并存；SDP 自带 startup 在 KVM 下存在 GICv3 NISV 缺陷，我们自建 startup board（`-fno-auto-inc-dec` 修复 + 反汇编门检查）并做 IFS 身份验收（dumpifs 尺寸指纹）
- **跨域 IPC 与时间基** — 64B echo smoke → 128B frame_v2 契约（13 项契约测试两端口径一致）→ 跨 VM 时间基校准闭环 → RTT 基线
- **QNX 调度 runtime + 真实推理链** — QNX 侧 6 线程固定优先级 runtime（算术释放/watchdog/deadline monitor/安全状态机），Linux broker 驱动真实 SparseDrive 4-engine 链（setTensorAddress 零拷贝接线），deadline-miss 统计与 7 段延迟分解
- **干扰实验 + 统一时间线** — Orin nsys 全内核清单 + QNX trace 合成统一 Perfetto 时间线；干扰实验矩阵（GPU 占空/CPU 核数/任务集/功耗）与 P50/95/99 分布，每个实验臂配机制解释
- **ITaskRuntime 双后端** — 同一 task-table 在 Linux（timerfd）/QNX（timer+sigwaitinfo）双后端加载，量化六大实测移植差异（优先级方向反转、TIMER_ABSTIME flag 面、标准信号复用陷阱等）, [docs/porting-diff.md](docs/porting-diff.md)
- **事件触发式负载** — QNX 状态机事件 → 一次性触发任务 → broker burst，与常驻链共存同一契约
- **在线闭环多模型调度** — release-gate（约 90 行）注入未改造 ROS 2 节点；三枚官方 Autoware 模型 + SparseDrive 主链全部由 QNX 释放驱动；真实 CARLA 相机流在 QNX 节奏下被 gated 链路处理
- **稳定性收官** — QNX runtime 多天连续运行零 QNX 侧故障，主动关闭收官；死因三分法与复跑规程: [docs/stability-postmortem-guide.md](docs/stability-postmortem-guide.md)


## What we demonstrated（定性）

- QNX guest 与真实 GPU 推理长期并存（非分钟级 smoke，跨重启/干扰事件的长期并存）
- 真实闭环：CARLA 相机 → QNX gate → 真实 4-engine 链 → 控制 → CARLA，车辆在被调度推理驱动下平稳巡航并自动脱困
- 多模型共存调度：3 枚官方 Autoware 模型与主链共存，验收窗口零 miss（定性；具体数字依 NCEULA 留私有，复测方法见 [bench/](bench/)）
- QNX runtime 多天连续运行零 QNX 侧故障（主动关闭收官，定性；数字留私有）
- release-gate 注入未改造 ROS 2 节点（节点逻辑零改动，调度权威留在 QNX）
- ITaskRuntime 双后端（Linux timerfd / QNX timer+sigwaitinfo）同一任务表加载，验收窗口均零 miss，移植差异成文
- 稳定性上限由仿真环境侧决定，不由 QNX 决定, [docs/stability-postmortem-guide.md](docs/stability-postmortem-guide.md)

## What this is NOT（诚实边界）

- 不是 DRIVE OS，也不是其复现。边界是 KVM（host-mediated），不是认证 Type-1 hypervisor；QNX guest 无 GPU 访问
- 不声称任何实时保证、determinism、freedom-from-interference、ASIL / ISO 26262
- 原生 QNX Hypervisor 路线在 Tegra234 上不可行（iGPU 无 SMMU stream ID、BPMP 时钟无 QNX client、A78AE 无嵌套 KVM），不在本项目范围
- 闭环为 CARLA 仿真器非实车；除 T1 外各模型输出不进控制路径（真实包络竞争负载）
- **QNX NCEULA 约束**: QNX SDP 8.0 依 BlackBerry QNX 非商业用途 EULA 使用；QNX 软件的评测结果（benchmark 数字）未经书面批准不得发布——本仓库只发布方法学与代码，定量结果不入库（.gitignore 强制）；QNX/BSP 派生二进制（startup、ifs.bin、镜像）不可再分发，每个使用者用自己 SDP 8.0 构建；使用者需自备并遵守自己的 QNX SDP license

## Repo layout

| 目录 | 内容 |
|---|---|
| `contract/` | wire contract 单一事实源（128B frame_v2），两端共享 |
| `qnx/` | QNX 侧: runtime（调度/监督）/ trace / timebase / smoke / IFS 构建 |
| `qnx/ifs/` | startup-qemu-virt 源码（带 NISV 修复与门检查）+ IFS 构建 recipe |
| `qnx/build/` | mkqnximage/IFS 构建局部配置 + 凭据生成 [misc_files/README.md](qnx/build/local/misc_files/README.md) |
| `linux/` | Linux 侧: broker / workloads / IPC / reference-client |
| `timeline/` | nsys profile + QNX trace → Perfetto 统一时间线 |
| `bench/` | 实验臂定义 / runner / 统计（P50/95/99、closure residual、稳定性监控） |
| `models/` | 模型下载表（link-only）: sparsedrive-trt-edge + Autoware 官方 ONNX |
| `results/` | 运行产物不入库（政策见 [results/README.md](results/README.md)） |
| `itask/` | ITaskRuntime Linux/QNX 双后端抽象 |
| `scripts/orin/` | bridge / launch（vCPU pinning）/ governor / 采集脚本 |
| `scripts/x86/` | 闭环 demo 启动器（环境变量驱动模板） |
| `g6_gate/` | release-gate 头文件（注入 ROS 2 节点用） |
| `docs/` | bring-up runbook、契约文档、闭环调度设计、移植差异、稳定性规程、阶段与验收 |
| `PROVENANCE.md` | 逐文件来源 + license 清单 |

## Get Started

本仓库分 x86 可验证部分与板端部分。x86 部分任何机器可跑通；板端部分需要 AGX Orin（JetPack R36.5）与自备的 QNX SDP 8.0（非商业用途 EULA）。

1. **契约测试（x86，无依赖）**

   ```bash
   cd contract && make && ./test_contract
   ```
   13 项契约测试全绿即两端口径一致。

2. **构建 QNX guest 镜像（SDP host）**

   ```bash
   # 需自备 QNX SDP 8.0 + BSP（build-ifs.sh 有 fail-fast 门禁）
   export BSP=<BSP_hyp-guest-arm_be-800_* install dir>
   bash qnx/build/build-ifs.sh
   # 生成 guest 凭据（不入库）: bash qnx/build/local/misc_files/gen_guest_keys.sh
   ```
   产物 ifs.bin/disk 镜像**不分发**，仅本机使用。

3. **板上启动**（AGX Orin, JetPack R36.5）: 见 [docs/bring-up-runbook.md](docs/bring-up-runbook.md) （bring-up 检查点、bridge/vCPU pinning/governor 设置），runtime 侧任务表在 qnx/stage/tasks*.csv。

4. **跑真实链 + 多模型调度**（可选，闭环 demo）: 引擎与 ONNX 下载/构建见 [models/README.md](models/README.md)；broker_chain 参数显式传入（见其 usage），x86 侧闭环栈用 [scripts/x86/run_local_g6.sh](scripts/x86/run_local_g6.sh)（环境变量模板，依赖 [sparsedrive-trt-edge](https://github.com/hous-lab/sparsedrive-trt-edge) 的 step5/step6 + CARLA）。稳定性监控与死因归因规程见 [docs/stability-postmortem-guide.md](docs/stability-postmortem-guide.md)。

5. **测量**（[bench/](bench/)）: 干扰实验矩阵（[bench/stats/g3_analyze.py](bench/stats/g3_analyze.py)）+ 跨域 Perfetto 时间线（[timeline/merge/to_perfetto.py](timeline/merge/to_perfetto.py)）。复测得到自己的数字。

## Hardware & software

| 项 | 值 |
|---|---|
| Board | NVIDIA AGX Orin (Tegra234), JetPack/L4T R36.5, MAXN |
| Guest | QNX SDP 8.0（非商业用途 EULA），KVM guest（2 vCPU / 1G 冻结） |
| Linux 侧 | TRT 10.x（引擎 fp16，板上构建），CARLA 0.9.15（可选闭环） |
| 迁移性 | 引擎/镜像环境派生不可迁移；代码与契约可迁移。数字与具体 TRT 版本强相关，不保证迁移性 |

## Acknowledgements

- [ChHaEoN/qnx-linux-dual-vm-proxy](https://github.com/ChHaEoN/qnx-linux-dual-vm-proxy)（MIT；build-qemu-virt.sh Apache-2.0）
- [sparsedrive-trt-edge](https://github.com/hous-lab/sparsedrive-trt-edge) — SparseDrive 主链 ONNX 导出与部署管线（兄弟仓）
- [Autoware](https://github.com/autowarefoundation/autoware_universe)
- [SparseDriveV2](https://github.com/swc-17/SparseDriveV2)（MIT）

## License

- MIT（见 LICENSE）
- 第三方来源与许可见 [PROVENANCE.md](PROVENANCE.md)
- QNX SDP 8.0 相关约束（非商业用途、评测结果发布限制、派生二进制不分发）见上方 NCEULA 段与 [PROVENANCE.md](PROVENANCE.md) 边界说明
- 本仓库为纯研究性质，用于非商业目的
