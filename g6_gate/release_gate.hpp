/* release_gate.hpp — P7 G6: QNX release-gate for an existing ROS 2 node.
 * A tiny TCP server on :7300 accepts the QNX runtime's T1 connection.
 * Each valid RELEASE frame permits ONE chain execution (non-blocking check
 * in the callback; no release -> skip). Enable via ROS param `release_gate`.
 * Contract: contract/frame_v2.h (shared single source of truth). */
#ifndef P7_RELEASE_GATE_HPP
#define P7_RELEASE_GATE_HPP

#include <tensorrt_sparsedrive/frame_v2.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <unistd.h>
#include <csignal>
#include <mutex>
#include <pthread.h>
#include <thread>

namespace p7 {

class ReleaseGate {
public:
  void start(int port)
  {
    signal(SIGPIPE, SIG_IGN);   /* stale QNX connections must not kill the node */
    enabled_ = true;
    std::thread([this, port] { serverLoop(port); }).detach();
  }
  /* non-blocking: consume one permit if available */
  bool acquire()
  {
    if (!enabled_.load()) return true;          /* gate disabled = legacy */
    std::lock_guard<std::mutex> lk(mu_);
    if (permits_ == 0) return false;
    --permits_;
    return true;
  }
  /* complete: send COMPLETE back to QNX with the Linux-side stamps */
  void complete(uint64_t start_ns, uint64_t end_ns)
  {
    if (!enabled_.load() || fd_ < 0) return;
    f2_frame_t r;
    memset(&r, 0, sizeof r);
    r.seq = last_seq_;
    r.ver = 0x0002;
    r.type = F2_TYPE_COMPLETE;
    r.task_id = F2_TASK_CHAIN;
    r.t_release = last_release_;
    r.t_tx = last_tx_;
    r.t_deadline = last_deadline_;
    r.t_rx = start_ns;
    r.t_exec = start_ns;
    r.t_pre_end = start_ns;
    r.t_infer_end = end_ns;
    r.t_post_end = end_ns;
    f2_seal(&r);
    uint8_t buf[F2_TOTAL_BYTES];
    f2_pack(&r, buf);
    size_t sent = 0;
    while (sent < F2_TOTAL_BYTES) {
      ssize_t w = write(fd_, buf + sent, F2_TOTAL_BYTES - sent);
      if (w <= 0) return;
      sent += (size_t)w;
    }
  }
  uint64_t acquired() const { return acquired_; }
  uint64_t skipped() const { return skipped_; }

private:
  void serverLoop(int port)
  {
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(s, (struct sockaddr *)&a, sizeof a);
    listen(s, 1);
    for (;;) {
      int c = accept(s, nullptr, nullptr);
      if (c < 0) continue;
      fd_ = c;
      uint8_t buf[F2_TOTAL_BYTES];
      for (;;) {
        size_t got = 0;
        while (got < F2_TOTAL_BYTES) {
          ssize_t n = read(c, buf + got, F2_TOTAL_BYTES - got);
          if (n <= 0) goto disc;
          got += (size_t)n;
        }
        f2_frame_t f;
        f2_unpack(buf, &f);
        if (f.type == F2_TYPE_RELEASE && f2_wire_crc_ok(buf)) {
          std::lock_guard<std::mutex> lk(mu_);
          permits_++;
          acquired_++;
        }
      }
    disc:
      close(c);
    }
  }

  std::atomic<bool> enabled_{false};
  std::atomic<uint64_t> acquired_{0}, skipped_{0};
  uint64_t permits_ = 0;
  uint64_t last_seq_ = 0, last_release_ = 0, last_tx_ = 0, last_deadline_ = 0;
  int fd_ = -1;
  std::mutex mu_;
};

} // namespace p7
#endif
