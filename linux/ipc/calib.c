/* calib.c — Linux-side timebase calibrator (frame_v2 PING/PONG).
 * delta = Q2 - (H1+H4)/2 (convention: T_qnx = H_linux + delta),
 * error bound = RTT/2 with min-RTT filter per 60s window;
 * drift = linear slope of delta(t). Usage: calib <ip> [n] [gap_ms] [csv] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "frame_v2.h"

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    const char *ip = argc > 1 ? argv[1] : "192.168.100.10";
    int n = argc > 2 ? atoi(argv[2]) : 1000;
    int gap_ms = argc > 3 ? atoi(argv[3]) : 20;
    const char *csv = argc > 4 ? argv[4] : "/tmp/calib_series.csv";
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(7100);
    inet_pton(AF_INET, ip, &a.sin_addr);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) { perror("connect"); return 1; }
    fprintf(stderr, "connected to %s:7100, n=%d gap=%dms\n", ip, n, gap_ms);

    FILE *out = fopen(csv, "w");
    fprintf(out, "idx,H1_ns,Q2_ns,H4_ns,rtt_ns,delta_ns\n");
    uint64_t *rtts = calloc(n, 8), *deltas = calloc(n, 8);
    uint64_t t_start = now_ns();
    for (int i = 0; i < n; ++i) {
        f2_frame_t p;
        memset(&p, 0, sizeof p);
        p.seq = (uint64_t)i;
        p.ver = 0x0002;
        p.type = F2_TYPE_PING;
        f2_seal(&p);
        uint8_t buf[F2_TOTAL_BYTES];
        f2_pack(&p, buf);
        uint64_t H1 = now_ns();
        size_t sent = 0;
        while (sent < F2_TOTAL_BYTES) {
            ssize_t w = write(s, buf + sent, F2_TOTAL_BYTES - sent);
            if (w <= 0) { perror("write"); return 1; }
            sent += (size_t)w;
        }
        size_t got = 0;
        memset(buf, 0, sizeof buf);
        while (got < F2_TOTAL_BYTES) {
            ssize_t r = read(s, buf + got, F2_TOTAL_BYTES - got);
            if (r <= 0) { perror("read"); return 1; }
            got += (size_t)r;
        }
        uint64_t H4 = now_ns();
        f2_frame_t g;
        f2_unpack(buf, &g);
        if (g.type != F2_TYPE_PONG || !f2_wire_crc_ok(buf)) {
            fprintf(stderr, "bad pong at %d\n", i);
            continue;
        }
        uint64_t Q2 = f2_get_u64(g.payload);
        uint64_t rtt = H4 - H1;
        int64_t delta = (int64_t)(Q2 - (H1 / 2 + H4 / 2));
        rtts[i] = rtt;
        deltas[i] = (uint64_t)delta;
        fprintf(out, "%d,%llu,%llu,%llu,%llu,%lld\n", i,
                (unsigned long long)H1, (unsigned long long)Q2,
                (unsigned long long)H4, (unsigned long long)rtt, (long long)delta);
        usleep((useconds_t)gap_ms * 1000);
    }
    fclose(out);
    uint64_t t_end = now_ns();
    qsort(rtts, n, 8, cmp_u64);
    printf("samples=%d rtt_p50=%llu rtt_min=%llu rtt_p99=%llu ns\n",
           n, (unsigned long long)rtts[n / 2], (unsigned long long)rtts[0],
           (unsigned long long)rtts[n * 99 / 100]);
    /* delta series: per-60s window, min-RTT delta */
    double dur_s = (double)(t_end - t_start) / 1e9;
    printf("duration=%.1fs\n", dur_s);
    /* drift: linear fit delta(t) using all samples (ns over s) */
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int m = n;
    for (int i = 0; i < m; ++i) {
        double x = dur_s * (double)i / (double)m;
        double y = (double)(int64_t)deltas[i];
        sx += x; sy += y; sxx += x * x; sxy = sxy + x * y;
    }
    double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    printf("delta_last=%lld ns  delta_slope=%.3f ns/s (%.3f ppm)  bound(min rtt/2)=%llu ns\n",
           (long long)(int64_t)deltas[m - 1], slope, slope / 1000.0,
           (unsigned long long)(rtts[0] / 2));
    return 0;
}
