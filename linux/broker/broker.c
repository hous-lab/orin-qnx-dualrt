/* broker.c - P7 Linux-side inference broker v1 (simulated workloads).
 * TCP server :7200; QNX runtime connects. On RELEASE: Linux stamps,
 * workload emulation, reply COMPLETE. Usage: broker [bind_ip] [csv] */
#include "frame_v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void sleep_ns(uint64_t ns)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / 1000000000ULL);
    ts.tv_nsec = (long)(ns % 1000000000ULL);
    nanosleep(&ts, NULL);
}

int main(int argc, char **argv)
{
    const char *bind_ip = argc > 1 ? argv[1] : "0.0.0.0";
    const char *csv = argc > 2 ? argv[2] : NULL;
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(7200);
    if (strcmp(bind_ip, "0.0.0.0") != 0)
        inet_pton(AF_INET, bind_ip, &a.sin_addr);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) { perror("bind"); return 1; }
    listen(s, 1);
    fprintf(stderr, "broker on %s:7200\n", bind_ip);
    FILE *out = csv ? fopen(csv, "w") : NULL;
    uint8_t buf[F2_TOTAL_BYTES];
    for (;;) {
        int c = accept(s, 0, 0);
        if (c < 0) continue;
        fprintf(stderr, "broker: qnx connected\n");
        for (;;) {
            size_t got = 0;
            while (got < F2_TOTAL_BYTES) {
                ssize_t n = read(c, buf + got, F2_TOTAL_BYTES - got);
                if (n <= 0) goto disc;
                got += (size_t)n;
            }
            f2_frame_t f;
            f2_unpack(buf, &f);
            if (f.type != F2_TYPE_RELEASE || !f2_wire_crc_ok(buf)) continue;
            uint64_t t_rx = now_ns();
            uint64_t t_exec = now_ns();
            sleep_ns(25000000ULL);   /* pre emulation 25ms */
            uint64_t t_pre_end = now_ns();
            sleep_ns(25000000ULL);   /* infer emulation 25ms */
            uint64_t t_infer_end = now_ns();
            uint64_t t_post_end = now_ns();
            f2_frame_t r;
            memset(&r, 0, sizeof r);
            r.seq = f.seq;
            r.ver = 0x0002;
            r.type = F2_TYPE_COMPLETE;
            r.task_id = F2_TASK_CHAIN;
            r.t_release = f.t_release;
            r.t_tx = f.t_tx;
            r.t_deadline = f.t_deadline;
            r.t_rx = t_rx;  r.t_exec = t_exec;
            r.t_pre_end = t_pre_end;
            r.t_infer_end = t_infer_end;
            r.t_post_end = t_post_end;
            f2_seal(&r);
            f2_pack(&r, buf);
            size_t sent = 0;
            while (sent < F2_TOTAL_BYTES) {
                ssize_t w = write(c, buf + sent, F2_TOTAL_BYTES - sent);
                if (w <= 0) goto disc;
                sent += (size_t)w;
            }
            if (out)
                fprintf(out, "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                        (unsigned long long)f.seq,
                        (unsigned long long)r.t_release,
                        (unsigned long long)r.t_tx,
                        (unsigned long long)r.t_deadline,
                        (unsigned long long)r.t_rx,
                        (unsigned long long)r.t_exec,
                        (unsigned long long)r.t_pre_end,
                        (unsigned long long)r.t_infer_end,
                        (unsigned long long)r.t_post_end);
        }
disc:
        close(c);
        fprintf(stderr, "broker: disconnected\n");
    }
}
