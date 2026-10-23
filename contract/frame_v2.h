/* frame_v2.h — single source of truth for the P7 runtime wire contract.
 *
 * Shared by QNX (qcc, C11) and Linux (gcc, C11). The struct is the
 * in-memory representation; the wire layout is written/read byte-by-byte
 * via explicit little-endian helpers. Static asserts pin struct layout
 * to wire offsets.
 *
 * 128-byte fixed frame, 9 timestamps, 7 telescoping segments:
 * sum of segments == t_rx_q - t_release exactly. The global clock
 * offset delta only places endpoints on one analysis timeline; it
 * never enters the identity.
 *
 * All timestamps are uint64 nanoseconds in the sender's local
 * monotonic clock (both sides use clock_gettime(CLOCK_MONOTONIC)).
 *
 * CRC: crc32c over bytes [0,88) and [96,128). Vector:
 * crc32c("123456789") == 0xE3069283.
 */
#ifndef ORIN_DUALOS_FRAME_V2_H
#define ORIN_DUALOS_FRAME_V2_H

#include <stdint.h>
#include <stddef.h>

#define F2_TOTAL_BYTES   128u
#define F2_PAYLOAD_BYTES 32u

#define F2_OFF_SEQ         0u
#define F2_OFF_VER         8u
#define F2_OFF_TYPE        10u
#define F2_OFF_FLAGS       11u
#define F2_OFF_TASK_ID     12u
#define F2_OFF_RSV         14u
#define F2_OFF_T_RELEASE  16u
#define F2_OFF_T_TX        24u
#define F2_OFF_T_DEADLINE 32u
#define F2_OFF_T_RX        40u
#define F2_OFF_T_EXEC     48u
#define F2_OFF_T_PRE_END  56u
#define F2_OFF_T_INFER_END 64u
#define F2_OFF_T_POST_END 72u
#define F2_OFF_T_RX_Q     80u
#define F2_OFF_ERR        88u
#define F2_OFF_CRC        92u
#define F2_OFF_PAYLOAD    96u

/* frame types */
#define F2_TYPE_RELEASE  0x01u
#define F2_TYPE_CANCEL   0x02u
#define F2_TYPE_COMPLETE 0x03u
#define F2_TYPE_PING     0x04u
#define F2_TYPE_PONG     0x05u
#define F2_TYPE_KICK     0x07u

/* flags */
#define F2_FLAG_DEADLINE_MISS 0x01u
#define F2_FLAG_DEGRADED      0x02u
#define F2_FLAG_WARMUP        0x04u
#define F2_FLAG_LAST_OF_RUN   0x08u

/* err codes (COMPLETE frames carry err; RELEASE always sends err=0) */
#define F2_ERR_OK             0u
#define F2_ERR_QUEUE_OVERFLOW 1u
#define F2_ERR_ENGINE_FAIL    2u
#define F2_ERR_CANCELLED      3u

/* task ids */
#define F2_TASK_NULL   0u
#define F2_TASK_CHAIN  1u
#define F2_TASK_DET30  2u
#define F2_TASK_SYNTH  3u
#define F2_TASK_TRIGGERED 4u

/* ---- in-memory representation ---- */
typedef struct {
    uint64_t seq;
    uint16_t ver;
    uint8_t  type;
    uint8_t  flags;
    uint16_t task_id;
    uint16_t rsv;
    uint64_t t_release;
    uint64_t t_tx;
    uint64_t t_deadline;
    uint64_t t_rx;
    uint64_t t_exec;
    uint64_t t_pre_end;
    uint64_t t_infer_end;
    uint64_t t_post_end;
    uint64_t t_rx_q;
    uint32_t err;
    uint32_t crc;
    uint8_t  payload[F2_PAYLOAD_BYTES];
} f2_frame_t;

/* layout pins: struct must match the wire offsets exactly */
#include <assert.h>
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define F2_STATIC_ASSERT(cond, tag) _Static_assert(cond, #tag)
#else
#define F2_STATIC_ASSERT(cond, tag) typedef char f2_assert_##tag[(cond) ? 1 : -1]
#endif

F2_STATIC_ASSERT(sizeof(f2_frame_t) == 128, size128);
F2_STATIC_ASSERT(offsetof(f2_frame_t, seq) == F2_OFF_SEQ, off_seq);
F2_STATIC_ASSERT(offsetof(f2_frame_t, ver) == F2_OFF_VER, off_ver);
F2_STATIC_ASSERT(offsetof(f2_frame_t, type) == F2_OFF_TYPE, off_type);
F2_STATIC_ASSERT(offsetof(f2_frame_t, flags) == F2_OFF_FLAGS, off_flags);
F2_STATIC_ASSERT(offsetof(f2_frame_t, task_id) == F2_OFF_TASK_ID, off_task);
F2_STATIC_ASSERT(offsetof(f2_frame_t, rsv) == F2_OFF_RSV, off_rsv);
F2_STATIC_ASSERT(offsetof(f2_frame_t, t_release) == F2_OFF_T_RELEASE, off_rel);
F2_STATIC_ASSERT(offsetof(f2_frame_t, t_tx) == F2_OFF_T_TX, off_tx);
F2_STATIC_ASSERT(offsetof(f2_frame_t, t_deadline) == F2_OFF_T_DEADLINE, off_dl);
F2_STATIC_ASSERT(offsetof(f2_frame_t, t_rx) == F2_OFF_T_RX, off_rx);
F2_STATIC_ASSERT(offsetof(f2_frame_t, t_exec) == F2_OFF_T_EXEC, off_exec);
F2_STATIC_ASSERT(offsetof(f2_frame_t, t_pre_end) == F2_OFF_T_PRE_END, off_pre);
F2_STATIC_ASSERT(offsetof(f2_frame_t, t_infer_end) == F2_OFF_T_INFER_END, off_inf);
F2_STATIC_ASSERT(offsetof(f2_frame_t, t_post_end) == F2_OFF_T_POST_END, off_post);
F2_STATIC_ASSERT(offsetof(f2_frame_t, t_rx_q) == F2_OFF_T_RX_Q, off_rxq);
F2_STATIC_ASSERT(offsetof(f2_frame_t, err) == F2_OFF_ERR, off_err);
F2_STATIC_ASSERT(offsetof(f2_frame_t, crc) == F2_OFF_CRC, off_crc);
F2_STATIC_ASSERT(offsetof(f2_frame_t, payload) == F2_OFF_PAYLOAD, off_pl);

/* ---- LE byte helpers ---- */
static inline void f2_put_u64(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)(v);         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);   p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 32);   p[5] = (uint8_t)(v >> 40);
    p[6] = (uint8_t)(v >> 48);   p[7] = (uint8_t)(v >> 56);
}

static inline uint64_t f2_get_u64(const uint8_t *p)
{
    return  (uint64_t)p[0]
         | ((uint64_t)p[1] << 8)
         | ((uint64_t)p[2] << 16)
         | ((uint64_t)p[3] << 24)
         | ((uint64_t)p[4] << 32)
         | ((uint64_t)p[5] << 40)
         | ((uint64_t)p[6] << 48)
         | ((uint64_t)p[7] << 56);
}

static inline void f2_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);   p[3] = (uint8_t)(v >> 24);
}

static inline uint32_t f2_get_u32(const uint8_t *p)
{
    return  (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static inline void f2_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
}

static inline uint16_t f2_get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* ---- pack / unpack (explicit LE) ---- */
/* Serialize struct to wire bytes. crc field is NOT written here;
 * f2_seal() sets it. memset the target buffer first if you want the
 * trailing 4 bytes [92,96) zeroed — actually [92,96) IS the crc field
 * per layout; there is no hole. All 128 bytes are defined. */
static inline void f2_pack(const f2_frame_t *f, uint8_t *buf)
{
    f2_put_u64(buf + F2_OFF_SEQ, f->seq);
    f2_put_u16(buf + F2_OFF_VER, f->ver);
    buf[F2_OFF_TYPE]  = f->type;
    buf[F2_OFF_FLAGS] = f->flags;
    f2_put_u16(buf + F2_OFF_TASK_ID, f->task_id);
    f2_put_u16(buf + F2_OFF_RSV, 0);
    f2_put_u64(buf + F2_OFF_T_RELEASE, f->t_release);
    f2_put_u64(buf + F2_OFF_T_TX, f->t_tx);
    f2_put_u64(buf + F2_OFF_T_DEADLINE, f->t_deadline);
    f2_put_u64(buf + F2_OFF_T_RX, f->t_rx);
    f2_put_u64(buf + F2_OFF_T_EXEC, f->t_exec);
    f2_put_u64(buf + F2_OFF_T_PRE_END, f->t_pre_end);
    f2_put_u64(buf + F2_OFF_T_INFER_END, f->t_infer_end);
    f2_put_u64(buf + F2_OFF_T_POST_END, f->t_post_end);
    f2_put_u64(buf + F2_OFF_T_RX_Q, f->t_rx_q);
    f2_put_u32(buf + F2_OFF_ERR, f->err);
    f2_put_u32(buf + F2_OFF_CRC, f->crc);
    for (unsigned i = 0; i < F2_PAYLOAD_BYTES; ++i)
        buf[F2_OFF_PAYLOAD + i] = f->payload[i];
}

static inline void f2_unpack(const uint8_t *buf, f2_frame_t *f)
{
    f->seq = f2_get_u64(buf + F2_OFF_SEQ);
    f->ver = f2_get_u16(buf + F2_OFF_VER);
    f->type = buf[F2_OFF_TYPE];
    f->flags = buf[F2_OFF_FLAGS];
    f->task_id = f2_get_u16(buf + F2_OFF_TASK_ID);
    f->rsv = 0;
    f->t_release = f2_get_u64(buf + F2_OFF_T_RELEASE);
    f->t_tx = f2_get_u64(buf + F2_OFF_T_TX);
    f->t_deadline = f2_get_u64(buf + F2_OFF_T_DEADLINE);
    f->t_rx = f2_get_u64(buf + F2_OFF_T_RX);
    f->t_exec = f2_get_u64(buf + F2_OFF_T_EXEC);
    f->t_pre_end = f2_get_u64(buf + F2_OFF_T_PRE_END);
    f->t_infer_end = f2_get_u64(buf + F2_OFF_T_INFER_END);
    f->t_post_end = f2_get_u64(buf + F2_OFF_T_POST_END);
    f->t_rx_q = f2_get_u64(buf + F2_OFF_T_RX_Q);
    f->err = f2_get_u32(buf + F2_OFF_ERR);
    f->crc = f2_get_u32(buf + F2_OFF_CRC);
    for (unsigned i = 0; i < F2_PAYLOAD_BYTES; ++i)
        f->payload[i] = buf[F2_OFF_PAYLOAD + i];
}

/* ---- crc32c (Castagnoli, reflected, poly 0x1EDC6F41) ----
 * Small table-free implementation; adequate for 128B frames. */
static inline uint32_t f2_crc32c_buf(uint32_t seed, const uint8_t *buf, size_t n)
{
    uint32_t crc = ~seed;
    for (size_t i = 0; i < n; ++i) {
        crc ^= buf[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* Serialize with crc field zeroed, then crc over [0,88) + [96,128). */
static inline uint32_t f2_crc_of(const f2_frame_t *f)
{
    uint8_t buf[F2_TOTAL_BYTES];
    f2_frame_t tmp = *f;
    tmp.crc = 0;
    f2_pack(&tmp, buf);
    uint32_t c = f2_crc32c_buf(0, buf, F2_OFF_ERR + 4u);
    c = f2_crc32c_buf(c, buf + F2_OFF_PAYLOAD, F2_PAYLOAD_BYTES);
    return c;
}

/* Sender side: fill the crc field. */
static inline void f2_seal(f2_frame_t *f)
{
    f->crc = f2_crc_of(f);
}

/* Receiver side: verify against the RAW WIRE buffer. This is the form
 * receivers must use: it sees every byte the peer actually sent, including
 * the reserved field (which unpack normalizes to 0 and would hide). */
static inline int f2_wire_crc_ok(const uint8_t *buf)
{
    uint32_t c = f2_crc32c_buf(0, buf, F2_OFF_ERR + 4u);
    c = f2_crc32c_buf(c, buf + F2_OFF_PAYLOAD, F2_PAYLOAD_BYTES);
    uint32_t got = f2_get_u32(buf + F2_OFF_CRC);
    return c == got;
}

/* Receiver side: verify. */
static inline int f2_crc_ok(const f2_frame_t *f)
{
    return f->crc == f2_crc_of(f);
}

/* ---- 7-segment latency decomposition ----
 * Every segment is a difference of consecutive stamps, so the sum
 * telescopes to t_rx_q - t_release with no clock offset inside the
 * identity. Overflow-safe unsigned subtraction. */
static inline uint64_t f2_elapsed(uint64_t end, uint64_t begin)
{
    return end - begin;
}

typedef struct {
    uint64_t t_sched;     /* t_tx - t_release            (QNX-internal) */
    uint64_t t_ipc_up;    /* t_rx - t_tx                 (crosses) */
    uint64_t t_queue;     /* t_exec - t_rx               (Linux) */
    uint64_t t_pre;       /* t_pre_end - t_exec          (Linux) */
    uint64_t t_infer;     /* t_infer_end - t_pre_end     (Linux) */
    uint64_t t_post;      /* t_post_end - t_infer_end    (Linux) */
    uint64_t t_ipc_down;  /* t_rx_q - t_post_end         (crosses) */
    uint64_t t_total;     /* t_rx_q - t_release */
} f2_segments_t;

static inline void f2_segments(const f2_frame_t *f, f2_segments_t *s)
{
    s->t_sched    = f2_elapsed(f->t_tx, f->t_release);
    s->t_ipc_up   = f2_elapsed(f->t_rx, f->t_tx);
    s->t_queue    = f2_elapsed(f->t_exec, f->t_rx);
    s->t_pre      = f2_elapsed(f->t_pre_end, f->t_exec);
    s->t_infer    = f2_elapsed(f->t_infer_end, f->t_pre_end);
    s->t_post     = f2_elapsed(f->t_post_end, f->t_infer_end);
    s->t_ipc_down = f2_elapsed(f->t_rx_q, f->t_post_end);
    s->t_total    = f2_elapsed(f->t_rx_q, f->t_release);
}

/* Clock sanity: stamps must be monotonic along the chain. Returns 0 if ok. */
static inline int f2_chain_ok(const f2_frame_t *f)
{
    if (f->t_tx < f->t_release) return 1;
    if (f->t_rx < f->t_tx) return 2;
    if (f->t_exec < f->t_rx) return 3;
    if (f->t_pre_end < f->t_exec) return 4;
    if (f->t_infer_end < f->t_pre_end) return 5;
    if (f->t_post_end < f->t_infer_end) return 6;
    if (f->t_rx_q < f->t_post_end) return 7;
    return 0;
}

/* min-RTT timebase calibration exchange (Cristian's algorithm):
 * QNX sends PING (t_tx at send), Linux replies PONG immediately.
 * delta = t_pong_host - t_ping_qnx - rtt/2 ... use with min-RTT filter:
 * keep the ping whose RTT is minimal in a window; error bound = rtt_min/2. */
static inline uint64_t f2_rtt_ns(const f2_frame_t *ping, const f2_frame_t *pong)
{
    /* Linux stamps t_rx on PING receipt and t_post_end at PONG send. */
    return f2_elapsed(pong->t_post_end, ping->t_rx);
}

#endif /* ORIN_DUALOS_FRAME_V2_H */
