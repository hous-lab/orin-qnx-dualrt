# results/ — 评测产物不入库

本目录存放运行期产物（CSV/JSON/时间线/图像）。**全部内容不入库**（.gitignore `results/**`），原因有二：

1. QNX NCEULA 4.6(i)：QNX 软件的评测结果未经 BlackBerry QNX 书面批准不得发布。本仓库只发布方法学与代码，定量结果留在私有侧。
2. 产物本质是环境特定的一次性测量，公开它们也不具备可迁移性。

## 复测指引（得到你自己的数字）

- 实验臂定义与 runner：bench/（臂矩阵 + 采样 + P50/95/99 统计与 closure residual）
- 稳定性监控/归因：bench/runner/stability_monitor.py、stuck_watchdog.py，规程见 docs/stability-postmortem-guide.md
- 路径与缓存：本目录的 CSV 命名与稳定性日志默认路径在 docs/stability-postmortem-guide.md §5 有说明
- 契约级 7 段分解的口径：docs/frame-contract.md

环境（板卡模式、nvpmodel/MAXN、TRT 版本、引擎来源）请如实记录在你自己的实验记录里，这是数字可解释的前提。
