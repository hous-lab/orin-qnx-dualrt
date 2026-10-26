# results/ - run artifacts are not committed

This directory holds runtime artifacts (CSV/JSON/timelines/images). **Nothing here is committed** (.gitignore `results/**`), for two reasons:

1. QNX NCEULA 4.6(i): evaluation results of QNX software must not be published without written approval from BlackBerry QNX. This repository publishes methodology and code only; quantitative results stay on the private side.
2. The artifacts are one-shot, environment-specific measurements; publishing them would not transfer anyway.

## Re-test guide (get your own numbers)

- Experiment-arm definitions and runner: [bench/](../bench) (arm matrix + sampling + P50/95/99 statistics and the closure residual)
- [bench/runner/stability_monitor.py](../bench/runner/stability_monitor.py), [stuck_watchdog.py](../bench/runner/stuck_watchdog.py); the procedure is in section 5 of [docs/stability-postmortem-guide.md](../docs/stability-postmortem-guide.md)
- Paths and caches: the CSV naming in this directory and the default stability-log path are described in section 5 of [docs/stability-postmortem-guide.md](../docs/stability-postmortem-guide.md)
- Contract-level 7-segment breakdown: [docs/frame-contract.md](../docs/frame-contract.md)

Record the environment (board mode, nvpmodel/MAXN, TRT version, engine provenance) faithfully in your own experiment log - that is the precondition for interpreting any number.

## Measurement-metadata block (attach beside every re-test result)

- hardware: board model and memory/power setup
- power/clock: nvpmodel mode (MAXN) + jetson_clocks state + CPU governor
- software: JetPack/L4T, QNX SDP 8.0, TensorRT version, engine build command source
- measurement: warmup count, measured iteration count, sampling window
- statistic: mean vs percentiles (P50/95/99) listed separately, never mixed in one table
- latency-scope: model inference latency (pure engine execution) vs end-to-end latency (including pre/post-processing / IPC / scheduling) listed separately, never mixed in one table
- reproducibility: verified / non-reproducible must be labeled and listed separately from verified results
- release-tag: record the corresponding release tag of this repo (v0.1.0 onward)
- task metrics of different models are never merged into one score; quantization results always pair the perf delta with the accuracy metric and its eval conditions (this repo does no quantization; applies when a re-test introduces it)
