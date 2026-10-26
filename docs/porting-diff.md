# porting-diff - ITaskRuntime Linux to QNX, as it actually went

The same [itask/include/itask/runtime.hpp](../itask/include/itask/runtime.hpp) (61 lines) + the same task-table format + the same telemetry schema, implemented as separate backends on each side. This file is the code-level evidence of "platform porting (Linux to QNX)".

## Quantified

| Item | Linux (timerfd_backend.cpp) | QNX (timer_backend.cpp) |
|---|---|---|
| file sizes | ~75 | ~85 |
| two-file diff | ~60 lines of substantive difference (structural, not a line-by-line translation) | |
| build chain | g++ -std=c++17 -lpthread | qcc -std=gnu++17 (no -lpthread!) |
| validation | zero misses in the acceptance window (x86) | zero misses in the acceptance window (AGX KVM guest) |

## Six real difference points (all hit in practice, none from books)

1. **Priority direction inversion**: abstraction 1 (most urgent) maps to Linux native 1, but to QNX native **255**. `map_priority(): return abstract;` vs `return 256 - abstract;` - copying priority numbers directly during porting instantaneously inverts scheduling behavior.
2. **Timer wait primitive**: Linux `timerfd_create + read()` (fd-based, epoll-able); QNX `timer_create(SIGEV_SIGNAL) + sigwaitinfo()` (signal-based). Production QNX conventionally uses TimerSettime + SIGEV_THREAD_ID (dedicated per thread); this demo uses the POSIX subset and documents its measured multi-task defect (see point 5).
3. **Absolute-anchoring flag on a different face**: the same "absolute time anchoring" intent becomes Linux `TFD_TIMER_ABSTIME` vs QNX `timer_settime(TIMER_ABSTIME, ...)`. Getting it wrong (omitting it on the QNX side) causes double counting of the phase, then a miss every frame (the whole window misses before the fix, zero after).
4. **pthread is in libc**: QNX 8 linking does not need -lpthread (passing it actually fails with cannot find -lpthread).
5. **The standard-signal timer multiplexing trap**: when multiple tasks share SIGALRM, sigwaitinfo consumes each other's expired events (standard signals are not queued and carry no timer identity), so the first QNX demo build with multiple tasks hung outright. Production answer = per-task RT signal / per-thread timer; the Linux-side timerfd is immune by construction.
6. **console/boot surface**: the Linux demo runs directly; the QNX guest needs the demo + task table packed into the IFS with a self-start line injected in startup.sh (serial redirection), output landing in host logs via /dev/ser1.

## can-claim

- the same abstract interface + task table compiles, runs, and produces isomorphic telemetry on both OSes
- every porting point has measured evidence (the before/after miss contrast is qualitative)

## cannot-claim

- not a full-API-surface migration (camera stack / IPC framework not included)
- the QNX backend demo has a single task table (the multi-task signal-multiplexing issue is recorded per point 5; the production answer is known)
