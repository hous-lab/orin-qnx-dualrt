/* test_contract.c — contract consistency tests, runnable on any host
 * (no board, no SDP needed). Build:
 *   gcc -std=c11 -Wall -Wextra -Werror -I. test_contract.c -o test_contract
 */
#include <stdio.h>
#include <string.h>
#include "frame_v2.h"

static int failures = 0;

static void check(int cond, const char *name)
{
    if (cond) {
        printf("PASS %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        failures++;
    }
}

static void mk_frame(f2_frame_t *f)
{
    memset(f, 0, sizeof(*f));
    f->seq = 0x1122334455667788ULL;
    f->ver = 0x0002;
    f->type = F2_TYPE_COMPLETE;
    f->flags = F2_FLAG_DEADLINE_MISS | F2_FLAG_WARMUP;
    f->task_id = F2_TASK_CHAIN;
    f->t_release = 1000;
    f->t_tx = 1100;
    f->t_deadline = 5000;
    f->t_rx = 1200;
    f->t_exec = 1300;
    f->t_pre_end = 1500;
    f->t_infer_end = 9000;
    f->t_post_end = 9100;
    f->t_rx_q = 9200;
    f->err = F2_ERR_OK;
    for (unsigned i = 0; i < F2_PAYLOAD_BYTES; ++i)
        f->payload[i] = (uint8_t)i;
    f2_seal(f);
}

int main(void)
{
    f2_frame_t f;
    uint8_t buf[F2_TOTAL_BYTES];

    /* 1. known crc32c vector */
    {
        uint32_t c = f2_crc32c_buf(0, (const uint8_t *)"123456789", 9);
        check(c == 0xE3069283u, "crc32c known vector 123456789");
    }

    /* 2. LE byte placement on the wire */
    mk_frame(&f);
    f2_pack(&f, buf);
    check(buf[0] == 0x88 && buf[1] == 0x77 && buf[7] == 0x11,
          "LE seq bytes at offset 0");
    check(buf[F2_OFF_VER] == 0x02 && buf[F2_OFF_VER + 1] == 0x00,
          "ver LE at offset 8");
    check(buf[F2_OFF_TYPE] == F2_TYPE_COMPLETE, "type byte at offset 10");
    check(buf[F2_OFF_PAYLOAD] == 0x00 && buf[F2_OFF_PAYLOAD + 31] == 0x1F,
          "payload bytes at offset 96");

    /* 3. roundtrip */
    {
        f2_frame_t g;
        f2_unpack(buf, &g);
        check(memcmp(&f, &g, sizeof(f)) == 0, "pack/unpack roundtrip");
    }

    /* 4. crc catches every single-byte corruption */
    {
        int undetected = 0;
        for (unsigned i = 0; i < F2_TOTAL_BYTES; ++i) {
            uint8_t saved = buf[i];
            buf[i] ^= 0x01;
            f2_frame_t g;
            f2_unpack(buf, &g);
            if (f2_wire_crc_ok(buf)) undetected++;
            buf[i] = saved;
        }
        check(undetected == 0, "wire crc detects all 128 single-byte flips");
    }

    /* 5. telescoping identity */
    {
        f2_frame_t g;
        f2_segments_t s;
        f2_unpack(buf, &g);
        f2_segments(&g, &s);
        uint64_t sum = s.t_sched + s.t_ipc_up + s.t_queue + s.t_pre
                     + s.t_infer + s.t_post + s.t_ipc_down;
        check(sum == s.t_total, "7-segment sum == t_total");
        check(s.t_total == 8200, "t_total matches constructed stamps");
    }

    /* 6. chain sanity */
    {
        f2_frame_t g;
        f2_unpack(buf, &g);
        check(f2_chain_ok(&g) == 0, "well-formed chain accepted");
        f2_frame_t h = g;
        h.t_rx = h.t_tx - 1;    /* rx before tx: broken */
        check(f2_chain_ok(&h) == 2, "broken chain rejected (rx<tx)");
    }

    /* 7. rtt helper */
    {
        f2_frame_t ping, pong;
        memset(&ping, 0, sizeof(ping));
        memset(&pong, 0, sizeof(pong));
        ping.t_rx = 5000;
        pong.t_post_end = 6300;
        check(f2_rtt_ns(&ping, &pong) == 1300, "rtt helper arithmetic");
    }

    /* 8. kick/ping frames pass crc too */
    {
        f2_frame_t k;
        memset(&k, 0, sizeof(k));
        k.type = F2_TYPE_KICK;
        k.seq = 0;
        f2_seal(&k);
        uint8_t kb[F2_TOTAL_BYTES];
        f2_pack(&k, kb);
        f2_frame_t g;
        f2_unpack(kb, &g);
        check(f2_crc_ok(&g), "kick frame crc ok");
    }

    if (failures) {
        printf("\n%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("\nALL CONTRACT TESTS PASS\n");
    return 0;
}
