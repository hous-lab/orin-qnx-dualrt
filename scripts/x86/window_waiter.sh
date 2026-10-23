#!/usr/bin/env bash
# window_waiter.sh - wait for a free GPU window, then start the closed-loop
# stack + the stability monitor. Optional convenience wrapper around
# run_local_g6.sh; every knob is an explicit input.
#
# Env:
#   REPO  repo root (default: two levels up from this script)
#   NIC   interface to carry the 239.255.0.0/24 multicast route (required)
#   GPU_FREE_MB / PROCESS_WATCH  window thresholds (defaults match our runs)
set -euo pipefail
REPO="${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}"
NIC="${NIC:?set NIC=<interface for the 239.255.0.0/24 route>}"
GPU_FREE_MB="${GPU_FREE_MB:-2000}"
PROCESS_WATCH="${PROCESS_WATCH:-ncu_targ}"

while true; do
  used=$(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | head -1)
  watch=$(pgrep -fc "$PROCESS_WATCH" 2>/dev/null || echo 0)
  if [ "$used" -lt "$GPU_FREE_MB" ] && [ "$watch" -eq 0 ]; then
    echo "[$(date +%H:%M:%S)] WINDOW OPEN (gpu=${used}MB, watch=$watch) - launching stack"
    break
  fi
  sleep 300
done

[ -f /tmp/run_local_g6.sh ] || cp "$REPO/scripts/x86/run_local_g6.sh" /tmp/run_local_g6.sh
chmod +x /tmp/run_local_g6.sh
ip route | grep -q 239 || sudo ip route add 239.255.0.0/24 dev "$NIC"
tmux kill-session -t carla_stack 2>/dev/null || true
tmux new-session -d -s carla_stack "bash /tmp/run_local_g6.sh 1 2>&1 | tee /tmp/carla_stack.log; sleep infinity"
sleep 140
pgrep -f carla_spawn_object[s] >/dev/null || { echo "spawn failed, abort"; exit 1; }
tmux new-session -d -s monitor "python3 $REPO/bench/runner/stability_monitor.py $REPO/results/g6/stability_log.csv 60"
echo "[$(date +%H:%M:%S)] STACK + MONITOR UP - closed loop resuming"
