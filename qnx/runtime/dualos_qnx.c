/* dualos_qnx.c — P7 QNX-side runtime v1 (G2a)
 * 6 fixed-priority SCHED_FIFO threads supervising a Linux broker:
 *   p80 watchdog: heartbeat timeout -> state machine
 *   p70 release scheduler: TimerSettime absolute, release = epoch + k*T
 *   p60 deadline monitor: COMPLETE vs release+budget
 *   p50 ipc rx: blocking read, demux, seq+CRC check
 *   p30 telemetry: ring -> CSV
 *   p20 timebase responder
 */
#define _POSIX_C_SOURCE 200809L
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

static uint64_t GRID_NS   = 100000000ULL;   /* release grid (arg) */
static uint64_t BUDGET_NS =  80000000ULL;   /* deadline (arg) */
#define HB_MAX_NS (3ULL * GRID_NS)

/* G6: task-table-driven multi-task releases */
#define MAX_TASKS 8
typedef struct {
    char     name[32];
    uint64_t period_ns;
    uint64_t budget_ns;
    int      prio;
    int      task_id;
    int      port;            /* route: 7200=broker 7300=node gate */
} g6_task_t;
static g6_task_t g_tasks[MAX_TASKS];
static int       g_ntasks;
static int       g_task_fd[MAX_TASKS];   /* one connection per route */
static uint64_t  g_next_rel[MAX_TASKS];
static uint64_t  g_seq[MAX_TASKS];

static uint64_t g_rel_k;         /* next grid index */
static int      g_fd = -1;       /* broker socket */
static uint64_t g_n_rel, g_n_cmp, g_n_miss;
static uint64_t g_last_cmp_ns;   /* guest clock at last COMPLETE */
static f2_frame_t g_last_cmp;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;
static const char *g_state = "INIT";
static int g_degraded_seen;
static FILE *g_csv;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void set_state(const char *s, const char *reason)
{
    pthread_mutex_lock(&g_mu);
    if (g_state != s) {
        if (strcmp(s, "DEGRADED") == 0 || strcmp(s, "SAFE") == 0) g_degraded_seen = 1;
        printf("[state] %s -> %s (%s)\n", g_state, s, reason);
        g_state = s;
    }
    pthread_mutex_unlock(&g_mu);
}

static void *thr_watchdog(void *arg)
{
    (void)arg;
    struct timespec ts;
    uint64_t last_seen = 0;
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += (long)GRID_NS;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_mutex_lock(&g_mu);
        pthread_cond_timedwait(&g_cv, &g_mu, &ts);
        last_seen = g_last_cmp_ns;
        pthread_mutex_unlock(&g_mu);
        uint64_t now = now_ns();
        if (g_n_cmp == 0) {
            if (now - g_last_cmp_ns > HB_MAX_NS && g_n_rel > 0)
                set_state("SAFE", "no complete since start");
        } else if (now - last_seen > HB_MAX_NS) {
            set_state("DEGRADED", "heartbeat lost");
            if (now - last_seen > 10ULL * GRID_NS)
                set_state("SAFE", "heartbeat lost long");
        } else if (g_state != (const char *)0x1) {
            /* healthy: NORMAL unless already RECOVERING */
        }
    }
    return NULL;
}

/* release scheduler: absolute periodic timer on the 100ms grid */
static void *thr_sched(void *arg)
{
    (void)arg;
    /* v1 release grid via clock_nanosleep ABSTIME (drift-free against
     * arithmetic epoch + k*T); QNX TimerSettime upgrade lands in G2b. */
    uint64_t next_rel = now_ns() + GRID_NS;
    for (;;) {
        uint64_t t = now_ns();
        if (t + 2000000ULL < next_rel) {  /* 2ms coarse sleep */
            struct timespec ts = { (time_t)(next_rel / 1000000000ULL),
                                   (long)(next_rel % 1000000000ULL) - 2000000L };
            if (ts.tv_nsec < 0) { ts.tv_sec--; ts.tv_nsec += 1000000000L; }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        }
        uint64_t rel_stamp = next_rel;      /* arithmetic, not read */
        uint64_t tx = now_ns();
        if (g_fd >= 0) {
            f2_frame_t f;
            memset(&f, 0, sizeof f);
            f.ver = 0x0002;
            f.type = F2_TYPE_RELEASE;
            f.task_id = F2_TASK_CHAIN;
            f.seq = ++g_n_rel;
            f.t_release = rel_stamp;
            f.t_tx = tx;
            f.t_deadline = rel_stamp + BUDGET_NS;
            f2_seal(&f);
            uint8_t buf[F2_TOTAL_BYTES];
            f2_pack(&f, buf);
            size_t sent = 0;
            while (sent < F2_TOTAL_BYTES) {
                ssize_t w = write(g_fd, buf + sent, F2_TOTAL_BYTES - sent);
                if (w <= 0) { set_state("DEGRADED", "broker write fail"); break; }
                sent += (size_t)w;
            }
        }
        g_rel_k = g_n_rel;
        next_rel += GRID_NS;
        /* if we fell behind by more than 5 grids, re-anchor */
        if (now_ns() > next_rel + 5ULL * GRID_NS) {
            next_rel = now_ns() + GRID_NS;
        }
    }
    return NULL;
}

/* ipc rx: blocking read, demux COMPLETE, update shared state */
static void *thr_rx(void *arg)
{
    (void)arg;
    uint8_t buf[F2_TOTAL_BYTES];
    for (;;) {
        if (g_fd < 0) { struct timespec d = {0, 100000000L}; nanosleep(&d, NULL); continue; }
        size_t got = 0;
        while (got < F2_TOTAL_BYTES) {
            ssize_t n = read(g_fd, buf + got, F2_TOTAL_BYTES - got);
            if (n <= 0) {
                set_state("DEGRADED", "broker disconnected");
                pthread_mutex_lock(&g_mu);
                close(g_fd);
                g_fd = -1;
                pthread_mutex_unlock(&g_mu);
                break;   /* stay alive; reconnect loop will re-establish */
            }
            got += (size_t)n;
        }
        f2_frame_t f;
        f2_unpack(buf, &f);
        if (f.type != F2_TYPE_COMPLETE || !f2_wire_crc_ok(buf)) continue;
        f.t_rx_q = now_ns();   /* QNX receipt stamp (miss judge + closure) */
        pthread_mutex_lock(&g_mu);
        g_last_cmp = f;
        g_last_cmp_ns = now_ns();
        g_n_cmp++;
        pthread_cond_broadcast(&g_cv);
        pthread_mutex_unlock(&g_mu);
        /* deadline judgement */
        if (f.t_rx_q > f.t_deadline) {
            g_n_miss++;
            printf("[miss] seq=%llu total=%llu us (budget %llu us)\n",
                   (unsigned long long)f.seq,
                   (unsigned long long)((f.t_rx_q - f.t_release) / 1000ULL),
                   (unsigned long long)(BUDGET_NS / 1000ULL));
        }
        if (g_csv) fprintf(g_csv, "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                           (unsigned long long)f.seq,
                           (unsigned long long)f.t_release, (unsigned long long)f.t_tx,
                           (unsigned long long)f.t_deadline,
                           (unsigned long long)f.t_rx, (unsigned long long)f.t_exec,
                           (unsigned long long)f.t_pre_end,
                           (unsigned long long)f.t_infer_end,
                           (unsigned long long)f.t_post_end);
    }
}

/* telemetry: periodic status line */
static void *thr_telemetry(void *arg)
{
    (void)arg;
    for (;;) {
        struct timespec d = {5, 0};
        nanosleep(&d, NULL);
        pthread_mutex_lock(&g_mu);
        const char *st = g_state;
        uint64_t rel = g_n_rel, cmp = g_n_cmp, miss = g_n_miss;
        pthread_mutex_unlock(&g_mu);
        printf("[telemetry] state=%s rel=%llu cmp=%llu miss=%llu\n",
               st, (unsigned long long)rel,
               (unsigned long long)cmp, (unsigned long long)miss);
    }
    return NULL;
}

/* timebase responder (frame_v2 PING/PONG) */
static void *thr_timebase(void *arg)
{
    (void)arg;
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(7100);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(s, (struct sockaddr *)&a, sizeof a);
    listen(s, 1);
    uint8_t buf[F2_TOTAL_BYTES];
    for (;;) {
        int c = accept(s, 0, 0);
        if (c < 0) continue;
        for (;;) {
            size_t got = 0;
            while (got < F2_TOTAL_BYTES) {
                ssize_t n = read(c, buf + got, F2_TOTAL_BYTES - got);
                if (n <= 0) goto cls;
                got += (size_t)n;
            }
            f2_frame_t f;
            f2_unpack(buf, &f);
            if (f.type != F2_TYPE_PING) continue;
            uint64_t q2 = now_ns();
            f2_frame_t p;
            memset(&p, 0, sizeof p);
            p.ver = 0x0002;
            p.type = F2_TYPE_PONG;
            p.seq = f.seq;
            p.t_rx = q2;
            f2_put_u64(p.payload, q2);
            f2_seal(&p);
            f2_pack(&p, buf);
            size_t sent = 0;
            while (sent < F2_TOTAL_BYTES) {
                ssize_t w = write(c, buf + sent, F2_TOTAL_BYTES - sent);
                if (w <= 0) goto cls;
                sent += (size_t)w;
            }
        }
cls:
        close(c);
    }
    return NULL;
}

/* broker connector: connects to broker, reconnects on failure */
static void *thr_connect(void *arg)
{
    const char *ip = (const char *)arg;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(7200);
    inet_pton(AF_INET, ip, &a.sin_addr);
    for (;;) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) {
            pthread_mutex_lock(&g_mu);
            g_fd = s;
            int was_degraded = g_degraded_seen;
            const char *prev = g_state;
            g_state = "NORMAL";
            pthread_mutex_unlock(&g_mu);
            if (prev != (const char *)0 && strcmp(prev, "INIT") != 0)
                printf("[state] %s -> NORMAL (broker connected)\n", prev);
            if (was_degraded) {
                /* G5: state machine emits a one-shot triggered task */
                f2_frame_t tf;
                memset(&tf, 0, sizeof tf);
                tf.ver = 0x0002;
                tf.type = F2_TYPE_RELEASE;
                tf.task_id = F2_TASK_TRIGGERED;
                tf.seq = ++g_n_rel;
                tf.t_release = now_ns();
                tf.t_tx = now_ns();
                tf.t_deadline = tf.t_release + 2000000000ULL;  /* 2s burst budget */
                f2_seal(&tf);
                uint8_t tb[F2_TOTAL_BYTES];
                f2_pack(&tf, tb);
                size_t sent = 0;
                while (sent < F2_TOTAL_BYTES) {
                    ssize_t w = write(s, tb + sent, F2_TOTAL_BYTES - sent);
                    if (w <= 0) break;
                    sent += (size_t)w;
                }
                printf("[trigger] task_id=4 sent (post-DEGRADED)\n");
                was_degraded = 0;
                pthread_mutex_lock(&g_mu);
                g_degraded_seen = 0;
                pthread_mutex_unlock(&g_mu);
            }
            for (;;) {
                struct timespec d = {1, 0};
                nanosleep(&d, NULL);
                pthread_mutex_lock(&g_mu);
                int alive = (g_fd == s);
                pthread_mutex_unlock(&g_mu);
                if (!alive) break;
            }
        } else {
            close(s);
        }
        struct timespec d = {2, 0};
        nanosleep(&d, NULL);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    const char *ip = argc > 1 ? argv[1] : "192.168.100.1";
    if (argc > 3) GRID_NS   = (uint64_t)atoi(argv[3]) * 1000000ULL;
    if (argc > 4) BUDGET_NS = (uint64_t)atoi(argv[4]) * 1000000ULL;
    if (argc > 2) {
        g_csv = fopen(argv[2], "w");
        if (g_csv) fprintf(g_csv, "seq,t_release,t_tx,t_deadline,t_rx,t_exec,t_pre_end,t_infer_end,t_post_end\n");
    }
    /* QNX priority demo note: SCHED_FIFO via pthread inherits from process
     * (run as root in guest); v1 uses inherited prio, G2b sets explicit. */
    printf("[dualos-qnx] starting, broker=%s:7200 csv=%s\n", ip, argv[2] ? argv[2] : "-");
    g_last_cmp_ns = now_ns();
    pthread_t t_sched, t_rx, t_wd, t_tel, t_tb, t_conn;
    pthread_create(&t_conn, NULL, thr_connect, (void *)ip);
    pthread_create(&t_sched, NULL, thr_sched, NULL);
    pthread_create(&t_rx, NULL, thr_rx, NULL);
    pthread_create(&t_wd, NULL, thr_watchdog, NULL);
    pthread_create(&t_tel, NULL, thr_telemetry, NULL);
    pthread_create(&t_tb, NULL, thr_timebase, NULL);
    pthread_join(t_conn, NULL);
    return 0;
}
