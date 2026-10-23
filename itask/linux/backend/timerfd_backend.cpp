/* timerfd_backend.cpp — Linux backend: timerfd(TFD_TIMER_ABSTIME) + SCHED_FIFO.
 * Native priority = abstract value directly (Linux SCHED_FIFO 1..99,
 * smaller = more urgent). */
#include "itask/runtime.hpp"
#include <pthread.h>
#include <sched.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <ctime>
#include <thread>

namespace itask {

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

class TimerfdRuntime : public ITaskRuntime {
public:
    const char* backend() const override { return "linux-timerfd-sfifo"; }
    int map_priority(int abstract) const override { return abstract; }  /* 1:1 */

    bool run(const std::vector<TaskSpec>& tasks, int seconds,
             const std::function<void(const TaskSpec&, const Stamp&)>& cb) override
    {
        uint64_t t0 = now_ns();
        uint64_t horizon = t0 + (uint64_t)seconds * 1000000000ULL;
        for (const auto& t : tasks) {
            int fd = timerfd_create(CLOCK_MONOTONIC, 0);
            struct itimerspec its {};
            uint64_t first = t0 + (uint64_t)t.period_ms * 1000000ULL;
            its.it_value.tv_sec = (time_t)(first / 1000000000ULL);
            its.it_value.tv_nsec = (long)(first % 1000000000ULL);
            its.it_interval.tv_sec = t.period_ms / 1000;
            its.it_interval.tv_nsec = (t.period_ms % 1000) * 1000000L;
            timerfd_settime(fd, TFD_TIMER_ABSTIME, &its, nullptr);
            std::thread([=, this] {
                sched_param sp{};
                sp.sched_priority = map_priority(t.priority);
                pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
                uint64_t k = 0;
                uint64_t exp;
                while (now_ns() < horizon) {
                    if (read(fd, &exp, sizeof exp) <= 0) break;
                    Stamp s;
                    s.release_ns = t0 + (++k) * (uint64_t)t.period_ms * 1000000ULL;
                    s.start_ns = now_ns();
                    s.end_ns = now_ns();   /* workload hook goes here */
                    s.deadline_miss = (s.end_ns - s.release_ns) >
                                      (uint64_t)t.budget_ms * 1000000ULL;
                    if (cb) cb(t, s);
                }
                close(fd);
            }).detach();
        }
        std::this_thread::sleep_for(std::chrono::seconds(seconds + 1));
        return true;
    }
};

using TimerRuntime = TimerfdRuntime;

} // namespace itask
