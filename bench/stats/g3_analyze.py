#!/usr/bin/env python3
"""g3_analyze.py — segment the single continuous frame CSV by arm windows.
Usage: g3_analyze.py <broker.csv> <manifest.csv> <outdir>"""
import sys, os, json, collections
csv, manifest, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(outdir, exist_ok=True)
rows = []
with open(csv) as f:
    next(f)
    for line in f:
        p = line.strip().split(",")
        if len(p) == 13:
            rows.append([int(x) for x in p])
# frame time on epoch axis: t_rx is Linux CLOCK_MONOTONIC ns; use the FIRST
# frame's rx as origin, map manifest epochs relative to first arm start
arms = []
with open(manifest) as f:
    next(f)
    for line in f:
        a, r, t0, t1 = line.strip().split(",")
        arms.append((a, int(r), float(t0), float(t1)))
if not rows or not arms:
    print("no data"); sys.exit(1)
epoch0 = arms[0][2]           # first arm start (epoch s)
rx0 = rows[0][4]              # first frame rx (ns)
# NOTE: assumes frames started flowing near first arm start (broker connected
# shortly before the matrix launched -- verified manually)
def pct(v, q): return v[min(int(len(v)*q), len(v)-1)]
results = {}
for a, r, t0, t1 in arms:
    lo = rx0 + (t0 - epoch0) * 1e9 - 5e8
    hi = rx0 + (t1 - epoch0) * 1e9 + 5e8
    sel = [x for x in rows if lo <= x[4] <= hi]
    if not sel:
        results[f"{a}_r{r}"] = {"frames": 0}
        continue
    stats = {"frames": len(sel)}
    for idx, nm in [(9,"bb"),(10,"head"),(11,"map"),(12,"motion")]:
        v = sorted(x[idx] for x in sel)
        stats[nm] = {"p50": v[len(v)//2]/1e6, "p95": pct(v,0.95)/1e6, "p99": pct(v,0.99)/1e6}
    e2e = sorted(x[8]-x[4] for x in sel)
    stats["e2e_broker"] = {"p50": e2e[len(e2e)//2]/1e6, "p95": pct(e2e,0.95)/1e6, "p99": pct(e2e,0.99)/1e6}
    results[f"{a}_r{r}"] = stats
with open(os.path.join(outdir, "g3_stats.json"), "w") as f:
    json.dump(results, f, indent=1)
# per-arm aggregate table (mean of 3 reps)
agg = collections.defaultdict(lambda: collections.defaultdict(list))
for k, st in results.items():
    arm = k.rsplit("_r", 1)[0]
    if st["frames"]:
        agg[arm]["e2e_p50"].append(st["e2e_broker"]["p50"])
        agg[arm]["e2e_p99"].append(st["e2e_broker"]["p99"])
        agg[arm]["bb_p50"].append(st["bb"]["p50"])
        agg[arm]["head_p50"].append(st["head"]["p50"])
        agg[arm]["map_p50"].append(st["map"]["p50"])
        agg[arm]["motion_p50"].append(st["motion"]["p50"])
print(f"{'arm':10s} {'frames/rep':>10s} {'E2E p50':>9s} {'E2E p99':>9s} {'bb':>7s} {'head':>7s} {'map':>7s} {'motion':>7s}")
for arm in ["baseline","g25","g50","g90","c2","c4"]:
    if arm not in agg: continue
    d = agg[arm]
    n = len(d["e2e_p50"])
    avg = lambda k: sum(d[k])/n
    print(f"{arm:10s} {n:>10d} {avg('e2e_p50'):>8.2f}m {avg('e2e_p99'):>8.2f}m {avg('bb_p50'):>6.2f} {avg('head_p50'):>6.2f} {avg('map_p50'):>6.2f} {avg('motion_p50'):>6.2f}")
