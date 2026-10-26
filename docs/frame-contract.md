# frame_v2 contract

Authoritative source: [contract/frame_v2.h](../contract/frame_v2.h) (shared by qcc and gcc); machine-readable schema: [contract/frame_v2.json](../contract/frame_v2.json). This file explains the design rationale.

## Timestamps and segments

9 timestamps, 7 segments, telescoping (consecutive timestamp differences form segments):

| segment | definition | clock |
|---|---|---|
| T_sched | t_tx - t_release | QNX |
| T_ipc_up | t_rx - t_tx | crosses |
| T_queue | t_exec - t_rx | Linux |
| T_pre / T_infer / T_post | exec -> pre_end -> infer_end -> post_end | Linux |
| T_ipc_down | t_rx_q - t_post_end | crosses |

## Why telescoping matters

Every segment is a difference of adjacent timestamps, so the 7 segments sum to t_rx_q - t_release **exactly, by construction**. Consequences:

- the closure residual is a pipeline sanity check, not a proof of synchronization accuracy
- synchronization accuracy is bounded separately by the "min-RTT/2 claim + cross-domain frequency cross-check" (measured on the reference RTT baseline)
- the reporting-facing 5-segment view maps as T_ipc = T_sched + T_ipc_up + T_ipc_down; plus T_queue/T_pre/T_infer/T_post
- the four Linux-internal segments never touch the cross-VM clock offset, so the breakdown quality does not depend on sync accuracy - the most defensible property in the whole timeline story
