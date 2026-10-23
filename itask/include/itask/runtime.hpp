/* runtime.hpp — ITaskRuntime: platform-abstract periodic task runtime.
 * ONE task table + ONE telemetry schema on BOTH ends (Linux/QNX).
 * The ONLY platform differences live in the two backend files —
 * docs/porting-diff.md quantifies exactly those differences. */
#ifndef ITASK_RUNTIME_HPP
#define ITASK_RUNTIME_HPP

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace itask {

struct TaskSpec {
    std::string name;
    int period_ms;
    int budget_ms;
    int priority;   /* abstract 1..99, 1 = most urgent (Linux convention);
                     * each backend maps this per-OS (the gotcha) */
};

struct Stamp {
    uint64_t release_ns;  /* arithmetic instant: epoch + k*period */
    uint64_t start_ns;
    uint64_t end_ns;
    bool     deadline_miss;
};

/* parse the shared task table (CSV: name,period_ms,budget_ms,priority) */
inline bool load_task_table(const std::string& path, std::vector<TaskSpec>* out)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char name[64];
        int p, b, pr;
        if (sscanf(line, "%63[^,],%d,%d,%d", name, &p, &b, &pr) == 4)
            out->push_back({name, p, b, pr});
    }
    fclose(f);
    return !out->empty();
}

class ITaskRuntime {
public:
    virtual ~ITaskRuntime() = default;
    /* backend identity, e.g. "linux-timerfd-sfifo" / "qnx-timersettime-sfifo" */
    virtual const char* backend() const = 0;
    /* abstract priority (1=most urgent) -> OS-native value */
    virtual int map_priority(int abstract) const = 0;
    /* run all tasks at their periods for `seconds`; cb fires per release */
    virtual bool run(const std::vector<TaskSpec>& tasks, int seconds,
                     const std::function<void(const TaskSpec&, const Stamp&)>& cb) = 0;
};

} // namespace itask
#endif
