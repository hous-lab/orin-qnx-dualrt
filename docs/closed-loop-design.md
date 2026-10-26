# Online closed-loop multi-model scheduling - design and conclusions

## Goal

Plant the QNX scheduling authority into a real CARLA online closed loop: the SparseDrive main chain plus several official Autoware perception models coexist, all inference is driven by QNX releases, and real multi-model contention is measured.

## Model roster (all official Autoware weights; sources and download table in [models/README.md](../models/README.md))

| Task | Model | Weights source | Role |
|---|---|---|---|
| T1 | SparseDrive 4-engine chain | self-produced ONNX, then TRT engines (see [models/README.md](../models/README.md)) | main chain, control path (drives CARLA) |
| T2 | yolox-tiny (2D detection) | official Autoware yolox-tiny.onnx | resident mid-granularity |
| T3 | traffic-light fine detection | official Autoware tlr_car_ped_yolox_s_batch_1.onnx | resident |
| T4 | traffic-light classification | official Autoware traffic_light_classifier_mobilenetv2_batch_1.onnx | high-frequency small model |
| T5 (extension, not integrated) | centerpoint-tiny backbone | official Autoware pts_backbone_neck_head_centerpoint_tiny.onnx | heavy competitor |

Honest boundary: except T1, no model output enters the control path - they are **real-envelope competing workloads** (real weights, real frequencies, real pre/post-processing), so the scheduling/interference measurements remain fully valid; T1 drives CARLA.

## Architecture: release-gate injection

```
CARLA(x86) -> bridge -> 6 camera topics -> [gate] tensorrt_sparsedrive ROS 2 node -> PID -> Ackermann -> CARLA
                                        ^ RELEASE(T1)
QNX runtime --RELEASE(T1..T4, each with own period/priority)--> node/broker-side gate client
                                        ^ RELEASE(T2..T4)
                   [gate] broker extension executors (T2-T4: dedicated CUDA stream + dedicated period)
```

- The ROS 2 node changes by ~100 lines: a release-gate client thread + a wait at the callback entry
- T2..T4 execute via broker_chain extension executors (each task: dedicated CUDA stream + dedicated period)
- QNX side: the task table grows from 1 to 4 entries (independent grid, independent priority)

## Stages and acceptance

| Stage | Content | Acceptance |
|---|---|
| Engine build | T2/T3/T4 engine builds (trtexec --fp16) + single-executor run | 4 models individually inferable |
| gate injection | release-gate injected into the ROS 2 node + the QNX multi-task task table | T1 driven at QNX cadence |
| closed loop | CARLA online closed loop + multi-model coexistence | closed loop drivable + contention measured |
| extension (not done) | centerpoint heavy competitor + triggered traffic light (red detected -> high-priority inference) | real "resident + triggered" |

## Risks (assessed at project start)

- ONNX-to-TRT build compatibility (yolox/mobilenetv2 standard operators, expected low risk; centerpoint voxel encoder has custom operators, backbone-only needs validation)
- The closed-loop bottleneck is the host-side loop rate; contention tension comes from co-located models, not from the loop rate
- the release-wait semantics the gate introduces (a missed release = skip or queue? answer: skip, consistent with the main-chain semantics)

## Engine-build conclusion (qualitative)

Three official models' TRT engines built successfully (fp16, Orin MAXN), four models individually inferable. Finding: the official ONNX files were fully compatible with the then-current TRT version (zero custom operators; yolox's EfficientNMS-TRT plugin loads as-is), and the measured small-model envelope was far smaller than estimated at project start - the contention tension comes mainly from co-location with the T1 main chain rather than among the small models (consistent with the interference-matrix gradient conclusion).

## Scheduling-core verification (qualitative)

QNX multi-task runtime ([qnx/runtime/dualos_qnx_g6.c](../qnx/runtime/dualos_qnx_g6.c), task-table driven, per-task period/priority/grid, route-level shared connection) + broker extension T2-T4 executors: within the acceptance window the per-task release/completion counts agree (qualitative: multi-task coexistence scheduling green). Pitfall log (all fixed, high methodology value): (1) the task table's 6 columns got overwritten by an old 4-column table, so parsing yielded 0 rows; (2) runtime stdout over the serial console was fully buffered, fixed with setvbuf; (3) the broker served connections serially over a single connection, so multi-task multi-connection starved, fixed by route-level shared connections; (4) the yolox ONNX embeds the EfficientNMS-TRT plugin, so initLibNvInferPlugins is required; (5) when an old guest died suddenly it left zombie connections (Send-Q never ACKed), so broker + guest must be restarted in pairs.

## Closed loop and closure (qualitative)

Real CARLA camera frames get processed by the gated chain at QNX cadence, and the closed loop is drivable. Stability closure and death-cause attribution: [docs/stability-postmortem-guide.md](stability-postmortem-guide.md) (qualitative conclusion: multi-day continuous operation with zero QNX-side faults; the ceiling is set by the simulation-environment side).
