# 在线闭环多模型调度 — 设计与结论

## 目标

把 QNX 调度权威植入真实 CARLA 在线闭环：SparseDrive 主链 + 多枚官方 Autoware 感知模型共存，全部推理由 QNX 释放驱动，测量真实多模型竞争。

## 模型名册（全部为 Autoware 官方权重；来源与下载表见 models/README.md）

| 任务 | 模型 | 权重来源 | 角色 |
|---|---|---|---|
| T1 | SparseDrive 4-engine 链 | 自产 ONNX→TRT 引擎（models/README.md） | 主链，控制路径（驱动 CARLA） |
| T2 | yolox-tiny (2D 检测) | Autoware 官方 yolox-tiny.onnx | 常驻中粒度 |
| T3 | 红绿灯细检测 | Autoware 官方 tlr_car_ped_yolox_s_batch_1.onnx | 常驻 |
| T4 | 红绿灯分类 | Autoware 官方 traffic_light_classifier_mobilenetv2_batch_1.onnx | 高频小模型 |
| T5（拓展，未接入） | centerpoint-tiny backbone | Autoware 官方 pts_backbone_neck_head_centerpoint_tiny.onnx | 重负载竞争源 |

诚实边界：除 T1 外各模型输出不进控制路径——它们是**真实包络的竞争负载**（真实权重、真实频率、真实前后处理），调度/干扰测量完全有效；T1 驱动 CARLA。

## 架构：release-gate 注入

```
CARLA(x86) → 桥 → 6 路相机 topic → [gate] tensorrt_sparsedrive ROS 2 节点 → PID → Ackermann → CARLA
                                        ▲ RELEASE(T1)
QNX runtime ──RELEASE(T1..T4, 各自 period/priority)──► 节点/broker 侧 gate 客户端
                                        ▲ RELEASE(T2..T4)
                   [gate] broker 扩展执行器（T2-T4: 独立 CUDA 流 + 独立 period）
```

- ROS 2 节点改动约 100 行：release-gate 客户端线程 + 回调入口等待
- T2..T4 由 broker_chain 扩展执行（每任务独立 CUDA 流 + 独立 period）
- QNX 侧：任务表从 1 项扩到 4 项（grid 独立、priority 独立）

## 阶段与验收

| 阶段 | 内容 | 验收 |
|---|---|---|
| 引擎构建 | T2/T3/T4 引擎构建（trtexec --fp16）+ 单执行器跑通 | 4 模型独立可推理 |
| gate 注入 | release-gate 注入 ROS 2 节点 + QNX 多任务任务表 | T1 由 QNX 节奏驱动 |
| 闭环 | CARLA 在线闭环 + 多模型共存 | 闭环能开 + 竞争实测 |
| 拓展（未做） | centerpoint 重负载 + 触发式红绿灯（红 detected → 高优先推理） | 真实"常驻+触发" |

## 风险（立项时评估）
- ONNX→TRT 构建兼容性（yolox/mobilenetv2 标准算子，预期低风险；centerpoint voxel encoder 有自定义算子，只取 backbone 需验证）
- 闭环瓶颈在主机侧闭环速率，竞争张力来自共置模型，不来自闭环速率
- gate 引入的释放等待语义（错过一次释放 = 跳过还是排队？→ 跳过，与主链语义一致）

## 引擎构建结论（定性）

三枚官方模型 TRT 引擎构建成功（fp16, Orin MAXN），四模型独立可推理。发现：官方 ONNX 与当时 TRT 版本全兼容（零自定义算子；yolox 的 EfficientNMS_TRT 插件加载即可），且小模型实测包络远小于立项预估——多模型竞争的张力主要来自与 T1 主链的共置而非小模型互相之间（与干扰实验矩阵的梯度结论一致）。

## 调度核心验证（定性）

QNX 多任务 runtime（qnx/runtime/dualos_qnx_g6.c，任务表驱动，每任务独立 period/priority/grid，路由级共享连接）+ broker 扩展 T2-T4 执行器：验收窗口内各任务释放/完成计数一致（定性：多任务共存调度绿）。踩坑记录（全部修复，方法学价值高）：①任务表 6 列被旧 4 列覆盖 → 解析 0；②runtime stdout 串口全缓冲 → setvbuf；③broker 单连接串行服务，多任务多连接饿死 → 改路由级共享连接；④yolox ONNX 内嵌 EfficientNMS_TRT 插件 → 需 initLibNvInferPlugins；⑤旧 guest 猝死留僵尸连接（Send-Q 不 ACK）→ broker+guest 必须成对重启。

## 闭环与收官（定性）

真实 CARLA 相机流在 QNX 节奏下被 gated 链路处理，闭环能开。稳定性收官与死因归因见 docs/stability-postmortem-guide.md（定性结论：多天连续运行零 QNX 侧故障，上限由仿真环境侧决定）。
