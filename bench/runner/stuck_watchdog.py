#!/usr/bin/env python3
"""stuck_watchdog.py — auto-recover the ego when it gets stuck in CARLA geometry.
Speed < STUCK_SPEED for STUCK_N consecutive samples -> teleport to nearest lane
waypoint (+0.5m). This isolates OUR stability experiment (QNX scheduling/IPC/
supervision) from P1 model-level driving quality limits.
Usage: stuck_watchdog.py [interval_s]"""
import sys, os, time
INTERVAL = int(sys.argv[1]) if len(sys.argv) > 1 else 20
STUCK_SPEED = 0.15
STUCK_N = 3
sys.path.append(os.environ.get("CARLA_ROOT", "") + "/PythonAPI/carla/dist")
import carla

c = carla.Client('localhost', 1403); c.set_timeout(45)
stuck = 0
last_pos = None
while True:
    try:
        w = c.get_world()
        v = None
        for a in range(6):
            vs = list(w.get_actors().filter('vehicle.*'))
            if vs: v = vs[0]; break
            w.wait_for_tick(10.0); time.sleep(2)
        if v:
            loc = v.get_transform().location
            vel = v.get_velocity()
            spd = (vel.x**2 + vel.y**2) ** 0.5
            moved = True
            if last_pos:
                moved = ((loc.x-last_pos[0])**2 + (loc.y-last_pos[1])**2) ** 0.5 > 0.3
            if spd < STUCK_SPEED and not moved:
                stuck += 1
            else:
                stuck = 0
            if stuck >= STUCK_N:
                m = w.get_map()
                wp = m.get_waypoint(loc)
                v.set_transform(carla.Transform(
                    carla.Location(wp.transform.location.x, wp.transform.location.y,
                                   wp.transform.location.z + 0.5),
                    wp.transform.rotation))
                print(f"[{time.strftime('%H:%M:%S')}] STUCK x{stuck} -> respawned to lane "
                      f"({wp.transform.location.x:.1f},{wp.transform.location.y:.1f})", flush=True)
                stuck = 0
            last_pos = (loc.x, loc.y)
    except Exception as e:
        print(f"watchdog err: {type(e).__name__}", flush=True)
    time.sleep(INTERVAL)
