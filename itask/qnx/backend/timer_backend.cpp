/* timer_backend.cpp — QNX backend: timer_create(SIGEV_SIGNAL) + sigwaitinfo
 * + SCHED_FIFO. Portable POSIX subset present on both OSes; the QNX-native
 * alternative (TimerSettime + SIGEV_THREAD_ID) and the priority-direction
 * inversion are documented in docs/porting-diff.md.
 * GOTCHA: QNX priority numbers are INVERTED vs Linux (bigger = more urgent):
 * native = 256 - abstract. */
#include "itask/runtime.hpp"
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <thread>

namespace itask {

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

class TimerRuntime : public ITaskRuntime {
public:
    const char* backend() const override { return "qnx-timersettime-sfifo"; }
    int map_priority(int abstract) const override
    {
        return 256 - abstract;   /* INVERTED vs Linux */
    }

    bool run(const std::vector<TaskSpec>& tasks, int seconds,
             const std::function<void(const TaskSpec&, const Stamp&)>& cb) override
    {
        uint64_t t0 = now_ns();
        uint64_t horizon = t0 + (uint64_t)seconds * 1000000000ULL;
        int ti = 0;
        for (const auto& t : tasks) {
            const int signo = SIGRTMIN + (ti++);
            std::thread([=, this] {
                struct sched_param sp{};
                sp.sched_priority = map_priority(t.priority);
                pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

                sigset_t mask;
                sigemptyset(&mask);
                sigaddset(&mask, signo);
                pthread_sigmask(SIG_BLOCK, &mask, NULL);   /* own per-task RT signal */
                timer_t tid;
                struct sigevent sev;
                memset(&sev, 0, sizeof sev);
                sev.sigev_notify = SIGEV_SIGNAL;
                sev.sigev_signo = signo;
                timer_create(CLOCK_MONOTONIC, &sev, &tid);
                struct itimerspec its;
                uint64_t first = t0 + (uint64_t)t.period_ms * 1000000ULL;
                its.it_value.tv_sec = (time_t)(first / 1000000000ULL);
                its.it_value.tv_nsec = (long)(first % 1000000000ULL);
                its.it_interval.tv_sec = t.period_ms / 1000;
                its.it_interval.tv_nsec = (t.period_ms % 1000) * 1000000L;
                timer_settime(tid, TIMER_ABSTIME, &its, NULL);  /* absolute anchor (Linux: TFD_TIMER_ABSTIME) */
                uint64_t k = 0;
                while (now_ns() < horizon) {
                    int sig;
                    do { sig = sigwaitinfo(&mask, NULL); } while (sig < 0 && errno == EINTR);
                    if (sig != signo) break;
                    Stamp s;
                    s.release_ns = t0 + (++k) * (uint64_t)t.period_ms * 1000000ULL;
                    s.start_ns = now_ns();
                    s.end_ns = now_ns();
                    s.deadline_miss = (s.end_ns - s.release_ns) >
                                      (uint64_t)t.budget_ms * 1000000ULL;
                    if (cb) cb(t, s);
                }
                timer_delete(tid);
            }).detach();
        }
        std::this_thread::sleep_for(std::chrono::seconds(seconds + 1));
        return true;
    }
};

} // namespace itask
