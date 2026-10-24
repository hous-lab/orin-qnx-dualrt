# porting-diff — ITaskRuntime Linux → QNX 实录

同一 [itask/include/itask/runtime.hpp](../itask/include/itask/runtime.hpp)（61 行）+ 同一 task-table 格式 + 同一 telemetry schema，两端后端各自落地。本文件是"平台迁移（Linux 到 QNX）"的代码级证据。

## 量化

| 项 | Linux (timerfd_backend.cpp) | QNX (timer_backend.cpp) |
|---|---|---|
| 文件行数 | ~75 | ~85 |
| 两文件 diff | ~60 行实质差异（结构性差异非逐行翻译） | |
| 构建链 | g++ -std=c++17 -lpthread | qcc -std=gnu++17（无 -lpthread！） |
| 验证 | 验收窗口零 miss（x86） | 验收窗口零 miss（AGX KVM guest） |

## 六个真实差异点（全部踩过，非书面知识）

1. **优先级方向反转**：抽象 1（最紧急）→ Linux native 1，→ QNX native **255**。`map_priority(): return abstract;` vs `return 256 - abstract;` — 移植时直接复制优先级数字会瞬间反转调度行为。
2. **定时器等待原语**：Linux `timerfd_create + read()`（fd 化，可进 epoll）；QNX `timer_create(SIGEV_SIGNAL) + sigwaitinfo()`（信号化）。生产 QNX 惯用 TimerSettime + SIGEV_THREAD_ID（每线程独占），本 demo 用 POSIX 子集并实测其多任务缺陷（见第 5 点）。
3. **绝对锚定 flag 不同面**：同一"绝对时间锚定"意图 → Linux `TFD_TIMER_ABSTIME` vs QNX `timer_settime(TIMER_ABSTIME, ...)`。传错（QNX 侧漏传）→ 相位双重计数 → 每帧 miss（修复前整窗全 miss，修复后零 miss）。
4. **pthread 在 libc**：QNX 8 链接不需要 -lpthread（给了反而 cannot find -lpthread）。
5. **标准信号定时器多路复用陷阱**：多任务共用 SIGALRM 时 sigwaitinfo 互相消费彼此的过期事件（标准信号不排队、不带定时器身份）→ QNX demo 首版多任务直接挂死。生产答案 = 每任务 RT 信号/每线程 timer；Linux 侧 timerfd 天然无此问题。
6. **console/启动面**：Linux demo 直接跑；QNX guest 需把 demo + task table 打进 IFS 并在 startup.sh 注入自启行（串口重定向），输出经 /dev/ser1 落宿主日志。

## can-claim
- 同一抽象接口 + 任务表在双 OS 编译、运行、产出同构 telemetry
- 迁移点全部有实测证据（修复前后 miss 对照为定性口径）

## cannot-claim
- 不是全 API 面迁移（相机栈/IPC 框架不在内）
- QNX 后端 demo 单任务表（多任务信号复用问题按第 5 点记录，生产方案已知）
