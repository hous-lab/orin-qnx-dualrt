#!/usr/bin/env python3
"""stability_monitor.py — P7 G6a-3 stability watcher.
Samples every INTERVAL: CARLA ego (pos/speed), QNX g6 telemetry, component
liveness, Orin stream rate. Appends to a CSV + prints state transitions.
Usage: stability_monitor.py <out.csv> [interval_s]
Env: ORIN_HOST (ssh target for the board, e.g. user@host; required)"""
import sys, os, time, subprocess, json

OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/stability.csv"
INTERVAL = int(sys.argv[2]) if len(sys.argv) > 2 else 60
ORIN_HOST = os.environ.get("ORIN_HOST", "")
CARLA_EGG = os.environ.get("CARLA_ROOT", "") + "/PythonAPI/carla/dist"

sys.path.append(CARLA_EGG)
import carla  # noqa

def carla_state():
    try:
        c = carla.Client("localhost", 1403)
        c.set_timeout(8)
        w = c.get_world()
        vs = list(w.get_actors().filter("vehicle.*"))
        if not vs:
            return "no_vehicle", 0.0, 0.0, 0.0, 0
        v = vs[0]
        loc = v.get_transform().location
        vel = v.get_velocity()
        spd = (vel.x**2 + vel.y**2) ** 0.5
        return "ok", loc.x, loc.y, spd, len(vs)
    except Exception as e:
        return f"carla_err:{type(e).__name__}", 0.0, 0.0, 0.0, -1

def orin_line():
    try:
        r = subprocess.run(
            ["ssh", ORIN_HOST, "tmux capture-pane -t qnx -p | grep '\\[g6\\]' | tail -1"],
            capture_output=True, text=True, timeout=20)
        line = r.stdout.strip()
        if "[g6]" in line:
            return line
        return "no_telemetry"
    except Exception as e:
        return f"ssh_err:{type(e).__name__}"

def liveness():
    out = {}
    out["bridge"] = "up" if subprocess.run(["pgrep", "-f", "carla_autoware_bridg[e]"],
                                           capture_output=True).returncode == 0 else "down"
    out["node"] = subprocess.run(
        ["ssh", ORIN_HOST, "pgrep -f tensorrt_sparsedrive_nod[e] >/dev/null && echo up || echo down"],
        capture_output=True, text=True, timeout=20).stdout.strip()
    out["guest"] = subprocess.run(
        ["ssh", ORIN_HOST, "ping -c1 -W1 192.168.100.10 >/dev/null 2>&1 && echo up || echo down"],
        capture_output=True, text=True, timeout=20).stdout.strip()
    try:
        r = subprocess.run(
            ["ssh", ORIN_HOST,
             "R1=$(cat /sys/class/net/l4tbr0/statistics/rx_bytes); sleep 3; "
             "R2=$(cat /sys/class/net/l4tbr0/statistics/rx_bytes); echo $(( (R2-R1)/3/1024 ))"],
            capture_output=True, text=True, timeout=25)
        out["rx_kbps"] = r.stdout.strip()
    except Exception:
        out["rx_kbps"] = "?"
    return out

def main():
    if not ORIN_HOST:
        sys.exit("ERROR: ORIN_HOST env not set (ssh target for the board)")
    cols = ["utc", "carla_state", "x", "y", "speed", "n_vehicles",
            "bridge", "node", "guest", "rx_kbps", "qnx_telemetry"]
    with open(OUT, "a") as f:
        f.write(",".join(cols) + "\n")
    prev = None
    while True:
        st, x, y, spd, n = carla_state()
        lv = liveness()
        tel = orin_line().replace(",", ";")
        row = [time.strftime("%Y-%m-%dT%H:%M:%S"), st, f"{x:.1f}", f"{y:.1f}",
               f"{spd:.2f}", str(n), lv["bridge"], lv["node"], lv["guest"],
               str(lv["rx_kbps"]), tel]
        with open(OUT, "a") as f:
            f.write(",".join(row) + "\n")
        state_sig = (st, lv["bridge"], lv["node"], lv["guest"])
        if state_sig != prev:
            print(time.strftime("%H:%M:%S"), "TRANSITION:", row[:10], flush=True)
            prev = state_sig
        time.sleep(INTERVAL)

if __name__ == "__main__":
    main()
