/* resp.c — QNX-side frame_v2 PING/PONG timebase responder (:7100).
 * On PING: stamp Q2 = clock_gettime(CLOCK_MONOTONIC) at reply, return PONG
 * with Q2 in payload[0..8) and t_rx. Linux computes
 *   delta = Q2 - (H1+H4)/2   (T_qnx = H_linux + delta)
 * with error bound = RTT/2, filtered by min-RTT per window (G1/E-004). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "frame_v2.h"

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(void)
{
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(7100);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) { perror("bind"); return 1; }
    listen(s, 1);
    fprintf(stderr, "timebase responder on :7100 (frame_v2 128B)\n");

    uint8_t buf[F2_TOTAL_BYTES];
    for (;;) {
        int c = accept(s, 0, 0);
        if (c < 0) continue;
        for (;;) {
            size_t got = 0;
            while (got < F2_TOTAL_BYTES) {
                ssize_t n = read(c, buf + got, F2_TOTAL_BYTES - got);
                if (n <= 0) goto conn_closed;
                got += (size_t)n;
            }
            f2_frame_t f;
            f2_unpack(buf, &f);
            if (f.type != F2_TYPE_PING) continue;
            uint64_t q2 = now_ns();
            f2_frame_t p;
            memset(&p, 0, sizeof p);
            p.seq = f.seq;
            p.ver = 0x0002;
            p.type = F2_TYPE_PONG;
            p.t_rx = q2;
            f2_put_u64(p.payload, q2);
            f2_seal(&p);
            f2_pack(&p, buf);
            size_t sent = 0;
            while (sent < F2_TOTAL_BYTES) {
                ssize_t n = write(c, buf + sent, F2_TOTAL_BYTES - sent);
                if (n <= 0) goto conn_closed;
                sent += (size_t)n;
            }
        }
conn_closed:
        close(c);
    }
}
