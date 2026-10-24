# 稳定性运行与死因归因指南

> 目的：回答"系统能稳定运行多久、挂掉的原因是环境侧还是 QNX 侧"，并给出复跑规程。
> 本文只保留方法学与定性结论；定量数据依 QNX NCEULA 4.6(i) 留在私有侧（见 [results/README.md](../results/README.md)）。

## 1. 定性结论

- **QNX 调度/监督/IPC 链路从未是瓶颈**：g6 runtime 以任务表驱动的固定优先级释放连续运行多天（横跨双机重启恢复、外部干扰、spawn 异常等事件），状态始终 NORMAL，guest 从未崩溃、从未掉线，零 QNX 侧故障，由我们主动关闭收官。
- **所有中断都发生在 x86/CARLA/bridge 侧**，共 4 类签名：
  1. bridge 主节点 exit 1 + 整树关闭（CARLA 客户端异常 → bridge 主动退出）
  2. CARLA 段错误 (signal 11) — 多次重启累积后的仿真不稳定
  3. carla_spawn_objects "context is invalid" — 外部拆除 ROS context
  4. ExternalShutdownException（多次复现）— 我们的 ROS graph 被外部任务拆除（决定性证据：同机另一任务共用同一 CARLA 实例，其生命周期事件与我们 bridge 的死亡时间完全相关）

- 稳定性上限由仿真环境侧决定，不由 QNX 决定。

## 2. 死因三分法（复跑时的归因决策树）

挂掉后先看 monitor CSV 的最后一行，按签名归类：

| 签名 | 归因 | 说明 |
|---|---|---|
| no_vehicle + stream→0 + bridge=down + spawn 报 "actor not found" + 车辆最后位置在地图边缘/悬空 | **地图走到头** | CARLA 世界里 actor 被销毁（冲出可行驶区/掉落），bridge 抛 actor 异常退出；CARLA 本体仍活 |
| 任意组件 down + CARLA 进程消失/换 PID + bridge 报 ExternalShutdownException | **外部任务干扰** | 另一任务共用同一 CARLA 实例/端口，其重启/拆世界/关机动作直接杀伤我方 |
| guest ping down / QNX runtime 无遥测 / broker down | **QNX 侧问题**（实验期内未发生） | 若发生才需查: dmesg OOM、QEMU 进程、bridge 组播路由 |

## 3. 干净复跑规程

前置：确认无他人 bridge（`pgrep -af "ros2 launch"`）、CARLA 归属干净（`ss -tlnp | grep <carla-port>`）、GPU 显存回落（nvidia-smi）。

```bash
# 0. 环境变量（复用 scripts/x86/run_local_g6.sh 的同一套）
export CARLA_ROOT=... E2E_SD_ROOT=... BRIDGE_WS=...
export ORIN_HOST=... ORIN_APP_DIR=... REPO=... NIC=<toward 239.255.0.0/24>

# 1. 两侧组播路由（机器重启后必失，重加）
sudo ip route add 239.255.0.0/24 dev "$NIC"
ssh "$ORIN_HOST" 'sudo -n ip route add 239.255.0.0/24 dev <bridge-nic-on-orin>'

# 2. x86 全栈（CARLA→bridge→压缩器；tmux 会话保持防 SIGHUP）
tmux new-session -d -s carla_stack \
  "bash "$REPO/scripts/x86/run_local_g6.sh" 1 2>&1 | tee /tmp/carla_stack.log; sleep infinity"

# 3. 等 bridge 就绪（CARLA 启动 + bridge 起来的窗口），确认 spawn 活着
sleep 140; pgrep -f carla_spawn_object[s] && echo SPAWN_OK

# 4. Orin 侧（g6 runtime + broker 若未存活）
ssh "$ORIN_HOST" 'tmux kill-session -t node 2>/dev/null
tmux new-session -d -s node "bash '"$ORIN_APP_DIR"'/start_orin_sparsedrive_g6.sh > /tmp/node_g6.log 2>&1"'

# 5. 启动监控
tmux new-session -d -s monitor "python3 "$REPO"/bench/runner/stability_monitor.py \
  "$REPO"/results/g6/stability_log.csv 60"

# 6. 验证闭环: 车辆移动 + chain 完成数爬升
#    用 bench/runner/stuck_watchdog.py 同款 CARLA client 读速度/位移即可
```

## 4. 停止与归档

```bash
tmux kill-session -t monitor carla_stack          # 停 x86
ssh "$ORIN_HOST" 'tmux kill-session -t node broker qnx'   # 停 Orin（可选，可长驻）
# 数据: results/g6/stability_log.csv → 私有侧复盘（NCEULA 约束见 results/README.md）
```

## 5. 数据采集与归因注意

- `results/g6/stability_log.csv`（监控器周期采样；干扰发生后的样本反映的是外部任务的状态而非我方 — 归因时注意）
- QNX 累计释放/完成计数即为"系统能稳定运行多久"的 QNX 侧答案来源：上限由 CARLA/桥侧决定，不由 QNX 决定。

## 6. 案例: 外部干扰的根因实锤与礼让规则

**根因实锤（定性）**：曾抓到正在运行的 bridge 工作目录位于另一任务的独立 checkout，其日志报 `self.carla_world.tick()` 超时 —— 两个任务的 bridge 都处于 synchronous_mode_wait_for_vehicle_control，互相等对方不会发出的控制指令，双双向 sim 施加各自节奏 → 双输。

**礼让规则（重要）**：
- 我方 gated 节点会以真实节奏向共享 ROS 域发布 control_cmd — 在对方任务运行期间这本身就是干扰源。**对方任务运行期间必须停掉我方 node + decompressor**（guest/broker 不涉 ROS，可保留）。
- 复跑前检查: `pgrep -af "ros2 launch"` 确认无他人 bridge；`ss -tlnp | grep <carla-port>` 确认 CARLA 归属。

## 7. 案例研究: 静止起步问题（debug 方法论示范）

现象（定性）：全栈四任务 NORMAL、控制指令内容正确（节点侧 `ros2 topic echo` 直测油门值正确且以门控节奏发布），但 CARLA 车辆满油门零位移（tick 在发生、控制被施加、但有效油门≈0）。此前同组件栈曾正常巡航过。

已排除：调度链路（四任务绿）、控制发布（值正确、节奏正常）、bridge 归属（清理混装 checkout）、ackermann 进程存活。

debug 清单（按优先级，方法论示范）:
1. **双桥/双栈残留**: 复跑前 `pgrep -af carla` + 检查每个 carla_* 进程的 cwd 归属，确保零残留
2. **hand_brake / manual_control 初始态**: 用 CARLA API 读 `v.get_control()`（同步模式下第二客户端难拿快照，需在 bridge 侧加日志或用 world.get_snapshot()）
3. **ackermann settings 差异**: 两套启动路径的 bridge 启动参数逐项 diff（settings.yaml 里有车辆物理参数）
4. **门控后控制频率下降的影响**: gate 化后控制节奏变慢 vs 原生 PID 的细粒度计时器 — 离散油门积分是否趋零（对比成功时的控制频率）
5. **出生点场景特异性**: 换 spawn point 排除

**根因（确诊）**：出生点把车辆嵌入几何体，物理不咬合。诊断链：控制内容正确 → ackermann_cmd 流通 → CARLA 侧满油门/零 hand_brake + 持续 tick + 零位移 = 物理未咬合（轮子悬空），非栈问题。修复：传送最近车道 waypoint (+0.5m) 立即咬合并起步；车辆直行出弯再次卡入几何体（模型 steer=0 直行是模型级已知驾驶质量限制）→ [bench/runner/stuck_watchdog.py](../bench/runner/stuck_watchdog.py) 自动脱困（低速持续若干采样 → 传送回最近车道点），将长程稳定性实验与模型级驾驶质量解耦。

**结论**：起步问题非栈缺陷（管线全程正确输出满油门），系 CARLA spawn 质量问题。监控器 + 脱困守望者 + 全栈常驻组合使长程稳定性实验可持续运行。

## 8. 结果政策

定量结果（连续运行时长、释放/完成计数、闭环巡航数据等）依 QNX NCEULA 4.6(i) 不在本仓库发布，采集与归因方法见本文 §5 与 [bench/runner/](../bench/runner)，复测即可得到自己的数字。
