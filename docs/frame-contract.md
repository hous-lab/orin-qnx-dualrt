# frame_v2 contract

权威源：contract/frame_v2.h（qcc 与 gcc 共享）；机器可读 schema：contract/frame_v2.json。本文件解释设计动机。

## 时间戳与分段

9 时间戳、7 段、telescoping（连续时间戳相减成段）：

| segment | definition | clock |
|---|---|---|
| T_sched | t_tx - t_release | QNX |
| T_ipc_up | t_rx - t_tx | crosses |
| T_queue | t_exec - t_rx | Linux |
| T_pre / T_infer / T_post | exec -> pre_end -> infer_end -> post_end | Linux |
| T_ipc_down | t_rx_q - t_post_end | crosses |

## Why telescoping matters

每段都是相邻时间戳之差，所以 7 段之和与 t_rx_q - t_release **按构造精确相等**。推论：

- closure residual 是管线 sanity check，不是同步精度证明
- 同步精度由"min-RTT/2 论断 + 跨域频率交叉验证"另行界定（在参考 RTT 基线上测量）
- 面向汇报的 5 段视图映射为：T_ipc = T_sched + T_ipc_up + T_ipc_down；再加 T_queue/T_pre/T_infer/T_post
- 四个 Linux 内部段永不接触跨 VM 时偏，所以分解质量不依赖同步精度——这是整条时间线故事里最站得住的性质
