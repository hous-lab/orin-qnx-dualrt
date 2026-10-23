#!/usr/bin/env python3
"""to_perfetto.py — merge broker frames (Linux clock) + QNX events (delta-corrected)
into a Perfetto JSON trace. Usage: to_perfetto.py <broker.csv> <delta_ns> <out.json>"""
import json
import sys

def main():
    csv, delta, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    rows = []
    with open(csv) as f:
        next(f)
        for line in f:
            p = line.strip().split(",")
            if len(p) == 13:
                rows.append([int(x) for x in p])
    if not rows:
        print("no frames"); return
    t0 = min(r[4] for r in rows)          # t_rx of first frame (Linux ns)
    events = []
    def slice_ev(name, tid, ts, dur, cat):
        events.append({
            "name": name, "ph": "X", "ts": (ts - t0) / 1e3,   # us
            "dur": dur / 1e3, "pid": 1, "tid": tid, "cat": cat,
            "args": {}})
    for r in rows:
        seq, rel, tx, dl, rx, ex, pe, ie, po = r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8]
        # Linux-internal slices (single clock, exact) -- all relative to t0
        slice_ev(f"pre#{seq}",   1, ex - t0, pe - ex, "linux")
        slice_ev(f"bb#{seq}",    1, pe - t0, r[9],  "cuda")
        slice_ev(f"head#{seq}",  1, pe - t0 + r[9], r[10], "cuda")
        slice_ev(f"map#{seq}",   1, pe - t0 + r[9] + r[10], r[11], "cuda")
        slice_ev(f"motion#{seq}",1, pe - t0 + r[9] + r[10] + r[11], r[12], "cuda")
        slice_ev(f"post#{seq}",  1, ie - t0, po - ie, "linux")
        # QNX events, delta-corrected onto the same axis
        rel_c = rel - delta
        tx_c  = tx - delta
        slice_ev(f"RELEASE#{seq}", 2, tx_c - t0, max((rx - (tx - delta)), 1000), "qnx")
        # deadline marker (instant event)
        events.append({"name": f"DL#{seq}", "ph": "i", "ts": ((dl - delta) - t0) / 1e3,
                       "s": "p", "pid": 2, "tid": 2, "cat": "qnx"})
    trace = {"traceEvents": events, "displayTimeUnit": "ms"}
    with open(out, "w") as f:
        json.dump(trace, f)
    print(f"perfetto trace: {len(events)} events -> {out}")

if __name__ == "__main__":
    main()
