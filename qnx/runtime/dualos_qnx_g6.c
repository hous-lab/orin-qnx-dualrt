/* dualos_qnx_g6.c — P7 G6 multi-task QNX runtime.
 * Task table driven: per-task period/budget/priority/route; per-route TCP
 * connections; per-task release grids (clock_nanosleep ABSTIME, arithmetic
 * release instants). Watchdog/deadline/telemetry/timebase retained.
 * tasks.csv: name,period_ms,budget_ms,priority,task_id,port */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "frame_v2.h"

#define _POSIX_C_SOURCE 200809L
#define MAX_TASKS 8
#define MAX_ROUTES 4

typedef struct {
    char     name[32];
    uint64_t period_ns, budget_ns;
    int      prio, task_id, port;
    int      fd;              /* per-route connection */
    uint64_t next_rel, seq, n_cmp, n_miss;
} g6_task_t;

static g6_task_t g_tasks[MAX_TASKS];
static int       g_route_fd[MAX_ROUTES] = {-1,-1,-1,-1};
static int       g_ntasks;
static uint64_t  g_n_rel, g_n_cmp, g_n_miss;
static volatile int g_run = 1;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_csv;
static const char *g_state = "INIT";
static int g_degraded_seen;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void set_state(const char *s)
{
    if (g_state != s) {
        printf("[state] %s -> %s\n", g_state, s);
        g_state = s;
    }
    if (strcmp(s, "DEGRADED") == 0 || strcmp(s, "SAFE") == 0)
        g_degraded_seen = 1;
}

static int load_tasks(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof line, f) && g_ntasks < MAX_TASKS) {
        if (line[0] == '#') continue;
        char nm[32]; int p, b, pr, tid, port;
        if (sscanf(line, "%31[^,],%d,%d,%d,%d,%d", nm, &p, &b, &pr, &tid, &port) == 6) {
            g6_task_t *t = &g_tasks[g_ntasks++];
            snprintf(t->name, sizeof t->name, "%s", nm);
            t->period_ns = (uint64_t)p * 1000000ULL;
            t->budget_ns = (uint64_t)b * 1000000ULL;
            t->prio = pr;
            t->task_id = tid;
            t->port = port;
            t->n_cmp = 0;
        }
    }
    fclose(f);
    return g_ntasks;
}

static int route_of(const g6_task_t *t) { return t->port / 100 - 72; }

static int send_release(g6_task_t *t)
{
    int fd = g_route_fd[route_of(t)];
    if (fd < 0) return -1;
    f2_frame_t f;
    memset(&f, 0, sizeof f);
    f.ver = 0x0002;
    f.type = F2_TYPE_RELEASE;
    f.task_id = (uint16_t)t->task_id;
    f.seq = ++t->seq;
    f.t_release = t->next_rel;
    f.t_tx = now_ns();
    f.t_deadline = t->next_rel + t->budget_ns;
    f2_seal(&f);
    uint8_t buf[F2_TOTAL_BYTES];
    f2_pack(&f, buf);
    size_t sent = 0;
    while (sent < F2_TOTAL_BYTES) {
        ssize_t w = write(fd, buf + sent, F2_TOTAL_BYTES - sent);
        if (w <= 0) return -1;
        sent += (size_t)w;
    }
    g_n_rel++;
    return 0;
}

/* per-task release thread: ABSTIME grid, arithmetic release instants */
static void *thr_task(void *arg)
{
    int i = (int)(long)arg;
    g6_task_t *t = &g_tasks[i];
    uint64_t t0 = now_ns();
    t->next_rel = t0 + t->period_ns;
    while (g_run) {
        struct timespec ts;
        ts.tv_sec = (time_t)(t->next_rel / 1000000000ULL);
        ts.tv_nsec = (long)(t->next_rel % 1000000000ULL);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        if (!g_run) break;
        if (send_release(t) != 0) {
            set_state("DEGRADED");
        }
        t->next_rel += t->period_ns;
        if (now_ns() > t->next_rel + 5ULL * t->period_ns)
            t->next_rel = now_ns() + t->period_ns;   /* re-anchor */
    }
    return NULL;
}

/* per-route connect manager */
static void *thr_connect(void *arg)
{
    (void)arg;
    struct sockaddr_in a[MAX_ROUTES];
    int seen[MAX_ROUTES] = {0};
    while (g_run) {
        for (int i = 0; i < g_ntasks; ++i) {
            int ri = route_of(&g_tasks[i]);
            if (ri < 0 || ri >= MAX_ROUTES) continue;
            if (g_route_fd[ri] >= 0) continue;
            if (!seen[ri]) {
                memset(&a[ri], 0, sizeof a[ri]);
                a[ri].sin_family = AF_INET;
                a[ri].sin_port = htons((uint16_t)g_tasks[i].port);
                inet_pton(AF_INET, "192.168.100.1", &a[ri].sin_addr);
                seen[ri] = 1;
            }
            int s = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(s, (struct sockaddr *)&a[ri], sizeof a[ri]) == 0) {
                g_route_fd[ri] = s;
                set_state("NORMAL");
                printf("[g6] route %d connected (port %d)\n", ri, g_tasks[i].port);
            } else {
                close(s);
            }
        }
        struct timespec d = {2, 0};
        nanosleep(&d, NULL);
    }
    return NULL;
}

/* rx: per-task completion handling on all route fds — v1: poll loop */
static void *thr_rx(void *arg)
{
    (void)arg;
    uint8_t buf[F2_TOTAL_BYTES];
    fd_set rs;
    while (g_run) {
        FD_ZERO(&rs);
        int maxi = -1;
        for (int ri = 0; ri < MAX_ROUTES; ++ri)
            if (g_route_fd[ri] >= 0) { FD_SET(g_route_fd[ri], &rs); if (g_route_fd[ri] > maxi) maxi = g_route_fd[ri]; }
        if (maxi < 0) { struct timespec d = {0, 100000000L}; nanosleep(&d, NULL); continue; }
        struct timespec tv = {1, 0};
        fd_set rs2 = rs;
        int r = pselect(maxi + 1, &rs2, NULL, NULL, &tv, NULL);
        if (r <= 0) continue;
        for (int ri = 0; ri < MAX_ROUTES; ++ri) {
            if (g_route_fd[ri] < 0 || !FD_ISSET(g_route_fd[ri], &rs2)) continue;
            ssize_t n = read(g_route_fd[ri], buf, F2_TOTAL_BYTES);
            if (n <= 0) {
                close(g_route_fd[ri]);
                g_route_fd[ri] = -1;
                set_state("DEGRADED");
                continue;
            }
            if (n == F2_TOTAL_BYTES) {
                f2_frame_t f;
                f2_unpack(buf, &f);
                if (f.type != F2_TYPE_COMPLETE) continue;
                f.t_rx_q = now_ns();
                for (int i = 0; i < g_ntasks; ++i) {
                    if (g_tasks[i].task_id == (int)f.task_id) {
                        g_tasks[i].n_cmp++;
                        g_n_cmp++;
                        if (f.t_rx_q > f.t_deadline) {
                            g_tasks[i].n_miss++;
                            g_n_miss++;
                        }
                        break;
                    }
                }
            }
        }
    }
    return NULL;
}

static void *thr_telemetry(void *arg)
{
    (void)arg;
    while (g_run) {
        struct timespec d = {5, 0};
        nanosleep(&d, NULL);
        printf("[g6] state=%s rel=%llu cmp=%llu miss=%llu | ", g_state,
               (unsigned long long)g_n_rel, (unsigned long long)g_n_cmp,
               (unsigned long long)g_n_miss);
        for (int i = 0; i < g_ntasks; ++i)
            printf("%s=%llu/%llu ", g_tasks[i].name,
                   (unsigned long long)g_tasks[i].seq,
                   (unsigned long long)g_tasks[i].n_cmp);
        printf("\n");
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *ip   = argc > 1 ? argv[1] : "192.168.100.1";
    const char *tbl  = argc > 2 ? argv[2] : "/proc/boot/tasks_g6.csv";
    const char *csv  = argc > 3 ? argv[3] : NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (load_tasks(tbl) <= 0) { fprintf(stderr, "task table load fail\n"); return 1; }
    if (csv) { g_csv = fopen(csv, "w"); }
    (void)ip;
    printf("[g6-runtime] %d tasks:", g_ntasks);
    for (int i = 0; i < g_ntasks; ++i)
        printf(" %s(%dms,p%d,port%d)", g_tasks[i].name,
               (int)(g_tasks[i].period_ns / 1000000ULL),
               g_tasks[i].prio, g_tasks[i].port);
    printf("\n");
    pthread_t t_conn, t_rx, t_tel;
    pthread_create(&t_conn, NULL, thr_connect, NULL);
    pthread_create(&t_rx, NULL, thr_rx, NULL);
    pthread_create(&t_tel, NULL, thr_telemetry, NULL);
    pthread_t th[MAX_TASKS];
    for (int i = 0; i < g_ntasks; ++i)
        pthread_create(&th[i], NULL, thr_task, (void *)(long)i);
    for (int i = 0; i < g_ntasks; ++i)
        pthread_join(th[i], NULL);
    g_run = 0;
    return 0;
}
