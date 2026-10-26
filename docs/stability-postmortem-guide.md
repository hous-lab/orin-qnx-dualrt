# Stability operation and death-cause attribution guide

> Purpose: answer "how long can the system run stably, and is the cause of a death on the environment side or the QNX side", and give the re-run procedure.
> This doc keeps only methodology and qualitative conclusions; quantitative data stays on the private side per QNX NCEULA 4.6(i) (see [results/README.md](../results/README.md)).

## 1. Qualitative conclusions

- **The QNX scheduling/supervision/IPC chain was never the bottleneck**: the g6 runtime - a task-table-driven fixed-priority release loop - ran continuously for multiple days (spanning dual-machine reboot recovery, external interference, spawn anomalies, and other events) with the state machine in NORMAL the whole time; the guest never crashed, never dropped off, zero QNX-side faults, and we concluded the experiment with a deliberate shutdown.
- **All interruptions happened on the x86/CARLA/bridge side**, with 4 signatures:
  1. bridge main node exit 1 + tearing down the whole tree (a CARLA client exception makes the bridge exit deliberately)
  2. CARLA segfault (signal 11) - simulation instability after many restarts
  3. carla_spawn_objects "context is invalid" - an external teardown of the ROS context
  4. ExternalShutdownException (reproduced multiple times) - our ROS graph was torn down by an external task (decisive evidence: another task on the same machine shared the same CARLA instance, and its lifecycle events correlate exactly with our bridge's death times)

- The stability ceiling is set by the simulation-environment side, not by QNX.

## 2. Death-cause triage (attribution decision tree for re-runs)

After a death, read the last row of the monitor CSV and classify by signature:

| Signature | Attribution | Notes |
|---|---|---|
| no_vehicle + stream to 0 + bridge down + spawn reports "actor not found" + the vehicle's last position at a map edge / floating | **reached the end of the map** | In the CARLA world the actor got destroyed (left the drivable area / fell off); the bridge throws an actor exception and exits; the CARLA process itself is still alive |
| any component down + the CARLA process disappears / changes PID + bridge reports ExternalShutdownException | **external-task interference** | Another task shares the same CARLA instance/port; its restart/teardown/shutdown actions kill us directly |
| guest ping down / no QNX runtime telemetry / broker down | **QNX-side problem** (did not occur during the experiment) | If it ever happens, check: dmesg OOM, the QEMU process, the bridge multicast route |

## 3. Clean re-run procedure

Prerequisites: confirm no foreign bridge (`pgrep -af "ros2 launch"`), clean CARLA ownership (`ss -tlnp | grep <carla-port>`), GPU memory back to baseline (nvidia-smi).

```bash
# 0. env vars (the same set as scripts/x86/run_local_g6.sh)
export CARLA_ROOT=... E2E_SD_ROOT=... BRIDGE_WS=...
export ORIN_HOST=... ORIN_APP_DIR=... REPO=... NIC=<toward 239.255.0.0/24>

# 1. multicast routes on both machines (lost on every reboot, re-add)
sudo ip route add 239.255.0.0/24 dev "$NIC"
ssh "$ORIN_HOST" 'sudo -n ip route add 239.255.0.0/24 dev <bridge-nic-on-orin>'

# 2. the x86 full stack (CARLA, bridge, decompressor; keep it in tmux against SIGHUP)
tmux new-session -d -s carla_stack \
  "bash "$REPO/scripts/x86/run_local_g6.sh" 1 2>&1 | tee /tmp/carla_stack.log; sleep infinity"

# 3. wait for bridge ready (the CARLA-start + bridge-up window), confirm spawn is alive
sleep 140; pgrep -f carla_spawn_object[s] && echo SPAWN_OK

# 4. Orin side (g6 runtime + broker if not already alive)
ssh "$ORIN_HOST" 'tmux kill-session -t node 2>/dev/null
tmux new-session -d -s node "bash '"$ORIN_APP_DIR"'/start_orin_sparsedrive_g6.sh > /tmp/node_g6.log 2>&1"'

# 5. start monitoring
tmux new-session -d -s monitor "python3 "$REPO"/bench/runner/stability_monitor.py \
  "$REPO"/results/g6/stability_log.csv 60"

# 6. verify the closed loop: the vehicle moves + the chain completion count climbs
#    any CARLA client like bench/runner/stuck_watchdog.py can read speed/displacement
```

## 4. Shutdown and archiving

```bash
tmux kill-session -t monitor carla_stack          # stop x86
ssh "$ORIN_HOST" 'tmux kill-session -t node broker qnx'   # stop Orin (optional; can stay resident)
# data: results/g6/stability_log.csv, post-mortem on the private side (NCEULA constraint in results/README.md)
```

## 5. Data collection and attribution notes

- `results/g6/stability_log.csv` (periodic sampler; samples after an interference event reflect the external task's state, not ours - be careful during attribution)
- The QNX cumulative release/completion counts are the QNX-side answer to "how long can the system run stably": the ceiling is set by the CARLA/bridge side, not by QNX.

## 6. Case: hard evidence of external interference and the courtesy rules

**Hard evidence (qualitative)**: we once caught a running bridge whose working directory was another task's separate checkout, with its log showing `self.carla_world.tick()` timeouts - two tasks' bridges both in synchronous_mode_wait_for_vehicle_control, each waiting for control commands the other will never send, each imposing its own cadence on the sim - both lose.

**Courtesy rules (important)**:
- Our gated node publishes control_cmd into the shared ROS domain at the real cadence - while the other task is running this is itself a source of interference. **While the other task runs, our node + decompressor must be stopped** (guest/broker touch no ROS and can stay up).
- Before re-running: `pgrep -af "ros2 launch"` confirms no foreign bridge; `ss -tlnp | grep <carla-port>` confirms CARLA ownership.

## 7. Case study: the stationary-start problem (a debugging-methodology demonstration)

Symptom (qualitative): the full stack with all four tasks NORMAL, control commands correct in content (measured directly with `ros2 topic echo` on the node side: throttle values correct, published at the gated cadence), but the CARLA vehicle applies full throttle with zero displacement (ticks happening, control being applied, yet effective throttle is about 0). The same component stack had cruised normally before.

Ruled out: the scheduling chain (four tasks green), control publishing (values correct, cadence normal), bridge ownership (mixed-in foreign checkout cleaned), ackermann process alive.

Debug checklist (priority order, a methodology demonstration):
1. **residual dual-bridge/dual-stack**: before re-running, `pgrep -af carla` + check each carla_* process's cwd ownership, ensure zero residue
2. **hand_brake / manual_control initial state**: read `v.get_control()` via the CARLA API (a second client can hardly grab a snapshot in synchronous mode; add logging on the bridge side or use world.get_snapshot())
3. **ackermann settings differences**: diff the bridge launch parameters item by item across the two launch paths (settings.yaml holds the vehicle physics parameters)
4. **effect of lower control frequency after gating**: the gated cadence is slower than the native PID's fine-grained timer - does the discretized throttle integration decay toward zero (compare against the control frequency of the successful run)
5. **spawn-point scenario specificity**: change the spawn point to rule it out

**Root cause (confirmed)**: the spawn point embedded the vehicle inside geometry, so physics never engaged. Diagnosis chain: control content correct, ackermann_cmd flowing, CARLA side shows full throttle / zero hand_brake + continuous ticks + zero displacement = physics not engaged (wheels floating), not a stack problem. Fix: teleport to the nearest lane waypoint (+0.5m) - engages and starts immediately; the vehicle then drives straight out of a curve and wedges into geometry again (the model driving straight with steer=0 is a known model-level driving-quality limit), so [bench/runner/stuck_watchdog.py](../bench/runner/stuck_watchdog.py) auto-recovers (sustained low speed over several samples, then teleport back to the nearest lane point), decoupling the long-horizon stability experiment from model-level driving quality.

**Conclusion**: the stationary-start problem is not a stack defect (the pipeline outputs full throttle correctly end to end); it is a CARLA spawn-quality issue. Monitor + stuck-watchdog + resident full stack together make the long-horizon stability experiment sustainable.

## 8. Results policy

Quantitative results (continuous-run duration, release/completion counts, closed-loop cruise data, etc.) are not published in this repository per QNX NCEULA 4.6(i). Collection and attribution methods are in sections 5 and [bench/runner/](../bench/runner) - re-test to get your own numbers.
