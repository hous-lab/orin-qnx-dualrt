#!/usr/bin/env bash
# ============================================================
# run_local_g6.sh - x86-side closed-loop demo launcher (optional, environment-specific)
#
# Drives the G6a online closed loop from the x86 host: CARLA + the
# CARLA-Autoware bridge + the image compressor. This is the launcher WE used
# for the closed-loop experiments; every path is an explicit input because
# they depend on the sibling sparsedrive-trt-edge checkout (steps 5/6) and a
# local CARLA install. Treat it as a template, not a turnkey script.
#
# Required env:
#   CARLA_ROOT    CARLA 0.9.15 root (CarlaUE4.sh, PythonAPI)
#   E2E_SD_ROOT   sparsedrive-trt-edge checkout (holds step5_autoware_x86 /
#                 step6_autoware_orin)
#   BRIDGE_WS     CARLA-Autoware bridge colcon workspace (carla_autoware_bridge)
# Optional env:
#   ORIN_HOST / ORIN_APP_DIR   printed in the final hint (Orin-side stack)
#
# Usage: bash run_local_g6.sh [CARLA(0/1)]
#   CARLA=1 (default): start CARLA; CARLA=0: assume it is already running
# ============================================================
set -eo pipefail
export COLCON_TRACE=0

: "${CARLA_ROOT:?set CARLA_ROOT to the CARLA 0.9.15 root}"
: "${E2E_SD_ROOT:?set E2E_SD_ROOT to the sparsedrive-trt-edge checkout}"
: "${BRIDGE_WS:?set BRIDGE_WS to the CARLA-Autoware bridge workspace}"
ORIN_HOST="${ORIN_HOST:-<orin-host>}"
ORIN_APP_DIR="${ORIN_APP_DIR:-<orin-app-dir>}"
STEP6=$E2E_SD_ROOT/step6_autoware_orin
IMG_PKG=$STEP6/image_bridge
CARLA_FLAG="${1:-1}"

export ROS_DOMAIN_ID=0
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/run/user/$(id -u)/gdm/Xauthority}"

echo "[0/4] clean stale nodes (compressor/bridge/inference pollute the topics)..."
pkill -9 -f vad_image_compressor 2>/dev/null || true
pkill -9 -f vad_speed_bridge 2>/dev/null || true
pkill -9 -f tensorrt_sparsedrive_node 2>/dev/null || true
pkill -9 -f tensorrt_vad 2>/dev/null || true
sleep 2

# 1. CARLA
if [ "$CARLA_FLAG" = "1" ]; then
    echo "[1/4] start CARLA (RenderOffScreen)..."
    export __GL_SYNC_TO_VBLANK=0
    cd "$CARLA_ROOT"
    ./CarlaUE4.sh -carla-rpc-port=1403 -prefernvidia -quality-level=Low -RenderOffScreen \
        > "$STEP6/logs/carla.log" 2>&1 &
    echo "  CARLA PID $!  (takes ~90s to be ready)"
    sleep 90
fi

# 2. Bridge
echo "[2/4] start CARLA-Autoware bridge..."
export PYTHONPATH="$E2E_SD_ROOT/step5_autoware_x86/scripts/py3_shims:$CARLA_ROOT/PythonAPI/carla:$CARLA_ROOT/PythonAPI/carla/dist:$PYTHONPATH"
cd "$BRIDGE_WS"
source install/setup.bash
ros2 launch carla_autoware_bridge carla_aw_bridge.launch.py port:=1403 town:=Town10HD timeout:=20 \
    > "$STEP6/logs/bridge.log" 2>&1 &
echo "  Bridge PID $!"
sleep 45

# 3. Compressor node (SparseDrive: packed frames)
echo "[3/4] start image compressor node..."
cd "$IMG_PKG" && source install/setup.bash
python3 vad_image_compressor/vad_image_compressor/image_compressor_node.py \
    > "$STEP6/logs/compressor.log" 2>&1 &
echo "  compressor PID $!"
sleep 3

echo ""
echo "x86-side stack up (speed topic is visible across hosts, no speed_bridge needed)."
echo "  Orin side: ssh $ORIN_HOST 'cd $ORIN_APP_DIR && bash start_orin_sparsedrive.sh'"
echo "  Verification: the sibling repo's closed-loop guide."
