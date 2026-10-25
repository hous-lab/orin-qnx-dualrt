# results/ — 评测产物不入库

本目录存放运行期产物（CSV/JSON/时间线/图像）。**全部内容不入库**（.gitignore `results/**`），原因有二：

1. QNX NCEULA 4.6(i)：QNX 软件的评测结果未经 BlackBerry QNX 书面批准不得发布。本仓库只发布方法学与代码，定量结果留在私有侧。
2. 产物本质是环境特定的一次性测量，公开它们也不具备可迁移性。

## 复测指引（得到你自己的数字）

- 实验臂定义与 runner：[bench/](../bench)（臂矩阵 + 采样 + P50/95/99 统计与 closure residual）
- [bench/runner/stability_monitor.py](../bench/runner/stability_monitor.py)、[stuck_watchdog.py](../bench/runner/stuck_watchdog.py)，规程见 [docs/stability-postmortem-guide.md](../docs/stability-postmortem-guide.md)
- 路径与缓存：本目录的 CSV 命名与稳定性日志默认路径在 [docs/stability-postmortem-guide.md](../docs/stability-postmortem-guide.md) §5 有说明
- 契约级 7 段分解的口径：[docs/frame-contract.md](../docs/frame-contract.md)

环境（板卡模式、nvpmodel/MAXN、TRT 版本、引擎来源）请如实记录在你自己的实验记录里，这是数字可解释的前提。

## 测量元数据块（复测结果旁必附）

- hardware: 板卡型号与内存/供电方式
- power/clock: nvpmodel 模式（MAXN）+ jetson_clocks 状态 + CPU governor
- software: JetPack/L4T、QNX SDP 8.0、TensorRT 版本、引擎构建命令来源
- measurement: warmup 次数、测量迭代数、采样窗口
- statistic: 均值 vs 百分位（P50/95/99）分列，不得混表
- latency-scope: 模型推理延迟（纯 engine 执行）与端到端延迟（含前后处理/IPC/调度）分列，不得混表
- 可复现性: 已验证 / 不可复现必须标注，并与已验证结果分开列
- release-tag: 记录对应的本仓 release tag（v0.1.0 起）
- 不同模型的任务指标不得合并成一个分数；量化结果必须配对精度指标与评测条件（本仓未做量化；复测引入量化时适用）
