/* itask_main.cpp — identical demo harness for BOTH backends.
 * usage: itask_demo <backend:linux|qnx> <task_table.csv> [seconds] */
#include "itask/runtime.hpp"
#ifdef BACKEND_LINUX
#include "timerfd_backend.cpp"
#elif defined(BACKEND_QNX)
#include "timer_backend.cpp"
#else
#error define BACKEND_LINUX or BACKEND_QNX
#endif
#include <cstring>
int main(int argc, char** argv)
{
    const char* table = argc > 2 ? argv[2] : "tasks.csv";
    int secs = argc > 3 ? atoi(argv[3]) : 10;
    std::vector<itask::TaskSpec> tasks;
    if (!itask::load_task_table(table, &tasks)) {
        fprintf(stderr, "cannot load %s\n", table); return 1;
    }
    itask::TimerRuntime rt;
    printf("backend=%s table=%s tasks=%zu\n", rt.backend(), table, tasks.size());
    for (auto& t : tasks)
        printf("  task %s period=%dms budget=%dms prio %d -> native %d\n",
               t.name.c_str(), t.period_ms, t.budget_ms, t.priority,
               rt.map_priority(t.priority));
    uint64_t n = 0, miss = 0;
    rt.run(tasks, secs, [&](const itask::TaskSpec& t, const itask::Stamp& s) {
        ++n; miss += s.deadline_miss;
        printf("[itask] %s rel=%llu start=%llu end=%llu miss=%d\n",
               t.name.c_str(), (unsigned long long)s.release_ns,
               (unsigned long long)s.start_ns, (unsigned long long)s.end_ns,
               (int)s.deadline_miss);
    });
    printf("SUMMARY backend=%s releases=%llu miss=%llu\n",
           rt.backend(), (unsigned long long)n, (unsigned long long)miss);
    return 0;
}
