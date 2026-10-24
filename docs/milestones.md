# 研究阶段与验收标准

研究按阶段推进，每阶段有书面验收标准（exit criteria）；阶段描述全部用技术内容表达。细节文档：[docs/bring-up-runbook.md](bring-up-runbook.md)（上板）、[docs/closed-loop-design.md](closed-loop-design.md)（闭环）、[docs/porting-diff.md](porting-diff.md)（双后端）、[docs/stability-postmortem-guide.md](stability-postmortem-guide.md)（稳定性）。

## 阶段 1 — 可行性：QNX guest 与 GPU 推理并存

在 AGX Orin R36.5 上以 KVM guest 形态运行 QNX 8.0，同时宿主侧跑真实 TensorRT 推理链。步骤见 [docs/bring-up-runbook.md](bring-up-runbook.md)。

验收：guest 启动到 Startup complete（io-sock + echo server 起来）；宿主侧真实 4-engine 链跑通；两者并存 30 分钟稳定；vCPU pinning 与 guest 内存冻结在启动脚本中固定；KVM 下 GIC Error(19)/NISV 两个已知风险的处置结论归档。

## 阶段 2 — 跨域 IPC 与时间基

建立跨 VM 的确定性 IPC 与时间基。

验收：64B echo smoke；契约测试在两端工具链（qcc/gcc）全绿；时间基校准闭环 30 分钟稳定；64B/128B RTT 基线（3 重复）；频率交叉验证 <1ppm，否则修正"同振荡器"论断。

## 阶段 3 — QNX 调度 runtime + 真实推理链

QNX 侧 6 线程固定优先级 runtime（算术释放/watchdog/deadline monitor/安全状态机）经 broker 驱动真实 SparseDrive 4-engine 链。

验收：runtime 在 pidin 可见；T1/T2/T3 以 10/30/10Hz 释放，各 ≥600 发、序列无空洞；首个 deadline-miss 统计可归因；watchdog 注入在 ≤1 个释放周期内检出；Perfetto 合并时间线可读；五段视图与 nsys NVTX 对上。

前置：QNX 定时器精度探针（1ms/100ms 步长）先行验证。

## 阶段 4 — 干扰实验 + 统一时间线

构建干扰实验矩阵并合成跨域时间线。

验收：≥6 实验臂 × 3 重复归档；Orin nsys 首跑导出；QNX trace 与 CUDA/NVTX 同轴的 Perfetto 时间线；每臂 P50/95/99 + closure residual；每臂有机制解释而非只有曲线。卫生规则：CSV 数字一律 nsys OFF 时采，nsys 只作时间线证据。

## 阶段 5 — ITaskRuntime 双后端

同一 task-table 与 telemetry schema 在 Linux/QNX 两端各自落地。

验收：runtime.hpp 接口 + Linux（timerfd+SCHED_FIFO）与 QNX（TimerSettime+固定优先级）后端两端编译；同一任务表两端加载；[docs/porting-diff.md](porting-diff.md) 成文（逐差异 + 原因，含优先级方向反转）。

## 阶段 6 — 事件触发式负载

QNX 状态机事件 → 一次性触发任务 → broker burst → 异步完成；状态机获得 TRIGGERED 路径，与常驻链共存同一契约。

验收：触发任务在状态机事件后到达 broker，burst 时长与触发配置一致，异步 COMPLETE 回流，常驻链不受扰动。

## 阶段 7 — 在线闭环多模型调度

把 QNX 调度权威接入真实 CARLA 在线闭环：SparseDrive 主链 + 多枚官方 Autoware 模型共存，全部推理由 QNX 释放驱动。设计与结论见 [docs/closed-loop-design.md](closed-loop-design.md)。

验收：三枚官方模型 TRT 引擎构建成功并可独立推理；release-gate 注入后主链由 QNX 节奏驱动；CARLA 相机流在 gated 链路下闭环能开，多模型竞争实测。

## 阶段 8 — 稳定性收官

长时间连续运行回答"系统能稳定多久、故障归因在哪侧"。

验收：QNX runtime 多天连续运行零 QNX 侧故障（主动关闭收官）；全部中断归因在外部仿真/桥侧；死因三分法与复跑规程成文（[docs/stability-postmortem-guide.md](stability-postmortem-guide.md)）。定量数字依 NCEULA 留私有（[results/README.md](../results/README.md)）。
