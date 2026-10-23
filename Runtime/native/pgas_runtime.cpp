#include "noqeri/pgas_runtime.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
constexpr uint32_t magic = 0x4e515047; // NQPG
constexpr uint32_t op_ping = 1, op_read = 2, op_write = 3, op_cas = 4,
                   op_add = 5, op_barrier = 6;
constexpr uint32_t max_clients = 64;
constexpr uint32_t max_slots = 1000000;
constexpr uint32_t max_timeout_ms = 60000;
constexpr size_t req_bytes = 32, response_bytes = 16;
static void put32(unsigned char *p, uint32_t x) {
  x = htonl(x); std::memcpy(p, &x, 4);
}
static uint32_t get32(const unsigned char *p) {
  uint32_t x; std::memcpy(&x, p, 4); return ntohl(x);
}
static void put64(unsigned char *p, uint64_t x) {
  put32(p, static_cast<uint32_t>(x >> 32));
  put32(p + 4, static_cast<uint32_t>(x));
}
static uint64_t get64(const unsigned char *p) {
  return (uint64_t(get32(p)) << 32) | get32(p + 4);
}
static int recv_full(int fd, void *buffer, size_t length) {
  auto *p = static_cast<unsigned char *>(buffer);
  for (size_t n = 0; n < length;) {
    ssize_t got = recv(fd, p + n, length - n, 0);
    if (got == 0) return -ECONNRESET;
    if (got < 0) { if (errno == EINTR) continue; return -errno; }
    n += static_cast<size_t>(got);
  }
  return 0;
}
static int send_full(int fd, const void *buffer, size_t length) {
  auto *p = static_cast<const unsigned char *>(buffer);
  for (size_t n = 0; n < length;) {
#ifdef MSG_NOSIGNAL
    ssize_t sent = send(fd, p + n, length - n, MSG_NOSIGNAL);
#else
    ssize_t sent = send(fd, p + n, length - n, 0);
#endif
    if (sent == 0) return -EPIPE;
    if (sent < 0) { if (errno == EINTR) continue; return -errno; }
    n += static_cast<size_t>(sent);
  }
  return 0;
}
static bool valid_token(const char *token) {
  if (!token) return false;
  size_t n = strnlen(token, 65);
  if (n < 16 || n > 64) return false;
  for (size_t i = 0; i < n; ++i)
    if (static_cast<unsigned char>(token[i]) < 33 || static_cast<unsigned char>(token[i]) > 126) return false;
  return true;
}
static void socket_timeout(int fd, uint32_t ms) {
  timeval tv{}; tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}
static void encode_response(unsigned char *out, int status, int64_t value) {
  put32(out, magic); put32(out + 4, static_cast<uint32_t>(status));
  put64(out + 8, static_cast<uint64_t>(value));
}
}

struct nq_pgas_server {
  int listener = -1;
  uint16_t port = 0;
  std::string token;
  std::mutex data_mu;
  std::vector<int64_t> values;
  std::mutex barrier_mu;
  std::condition_variable barrier_cv;
  uint32_t participants = 0, arrivals = 0;
  uint64_t generation = 0;
  int barrier_result = 0;
  std::atomic<bool> stopping{false};
  std::thread accept_thread;
  std::mutex clients_mu;
  std::vector<int> sockets;
  std::vector<std::thread> workers;
};
struct nq_pgas_client {
  int fd = -1;
  std::mutex mu;
  uint32_t timeout_ms = 0;
};

namespace {
static int wait_barrier(nq_pgas_server *s) {
  if (s->participants == 0) return -ENOTSUP;
  std::unique_lock<std::mutex> lock(s->barrier_mu);
  if (s->stopping) return -ESHUTDOWN;
  // Broken barriers remain broken; a later generation cannot silently
  // turn a timed-out previous generation into a success response.
  if (s->barrier_result < 0) return s->barrier_result;
  const uint64_t initial = s->generation;
  if (++s->arrivals == s->participants) {
    s->arrivals = 0; ++s->generation; s->barrier_result = 0;
    s->barrier_cv.notify_all(); return 0;
  }
  bool done = s->barrier_cv.wait_for(lock, std::chrono::seconds(5), [&] {
    return s->stopping || s->generation != initial;
  });
  if (s->stopping) return -ESHUTDOWN;
  if (!done && s->generation == initial) {
    s->arrivals = 0; ++s->generation; s->barrier_result = -ETIMEDOUT;
    s->barrier_cv.notify_all(); return -ETIMEDOUT;
  }
  return s->barrier_result;
}
static void disconnect_client(nq_pgas_server *s, int fd) {
  // Remove the descriptor before closing it to avoid shutdown on a recycled fd.
  std::lock_guard<std::mutex> lock(s->clients_mu);
  auto it = std::find(s->sockets.begin(), s->sockets.end(), fd);
  if (it != s->sockets.end()) s->sockets.erase(it);
  shutdown(fd, SHUT_RDWR); close(fd);
}
static void serve_client(nq_pgas_server *s, int fd) {
  // Fixed-length token exchange, bounded before any RPC is accepted.
  socket_timeout(fd, 5000);
  unsigned char length_buf[4];
  int error = recv_full(fd, length_buf, sizeof(length_buf));
  uint32_t length = error ? 0 : get32(length_buf);
  unsigned char token_buf[64]{};
  if (!error && (length < 16 || length > 64)) error = -EACCES;
  if (!error) error = recv_full(fd, token_buf, length);
  unsigned char reply[response_bytes];
  // Constant-time comparison for equal-length tokens.
  unsigned diff = static_cast<unsigned>(length ^ s->token.size());
  for (size_t i = 0; i < 64; ++i) {
    const unsigned char lhs = i < length ? token_buf[i] : 0;
    const unsigned char rhs = i < s->token.size() ? static_cast<unsigned char>(s->token[i]) : 0;
    diff |= static_cast<unsigned>(lhs ^ rhs);
  }
  if (!error && diff) error = -EACCES;
  encode_response(reply, error, 0);
  if (send_full(fd, reply, sizeof(reply)) != 0 || error) { disconnect_client(s, fd); return; }
  // The read timeout bounds dead clients. Caller reconnects for further work.
  for (;;) {
    unsigned char req[req_bytes];
    if (recv_full(fd, req, sizeof(req)) != 0) break;
    int status = 0;
    int64_t result = 0;
    uint32_t operation = get32(req + 4);
    uint32_t index = get32(req + 8);
    int64_t arg1 = static_cast<int64_t>(get64(req + 16));
    int64_t arg2 = static_cast<int64_t>(get64(req + 24));
    if (get32(req) != magic || get32(req + 12) != 0) status = -EPROTO;
    else if (operation == op_ping) {}
    else if (operation == op_barrier) status = wait_barrier(s);
    else if (operation < op_read || operation > op_add) status = -ENOTSUP;
    else {
      std::lock_guard<std::mutex> lock(s->data_mu);
      if (index >= s->values.size()) status = -ERANGE;
      else {
        int64_t &slot = s->values[index];
        switch (operation) {
        case op_read: result = slot; break;
        case op_write: slot = arg1; break;
        case op_cas: result = slot; if (slot == arg1) slot = arg2; break;
        case op_add: {
          result = slot;
          // Explicit two's-complement wrapping arithmetic avoids signed UB.
          slot = static_cast<int64_t>(static_cast<uint64_t>(slot) + static_cast<uint64_t>(arg1));
          break;
        }
        default: break;
        }
      }
    }
    encode_response(reply, status, result);
    if (send_full(fd, reply, sizeof(reply)) != 0) break;
    if (status == -EPROTO) break;
  }
  disconnect_client(s, fd);
}
static void accept_loop(nq_pgas_server *s) {
  while (!s->stopping) {
    pollfd pfd{s->listener, POLLIN, 0};
    int rc = poll(&pfd, 1, 200);
    if (rc < 0) { if (errno == EINTR) continue; break; }
    if (rc == 0 || !(pfd.revents & POLLIN)) continue;
    int fd = accept(s->listener, nullptr, nullptr);
    if (fd < 0) { if (errno == EINTR) continue; if (s->stopping) break; continue; }
    std::lock_guard<std::mutex> lock(s->clients_mu);
    if (s->workers.size() >= max_clients) {
      shutdown(fd, SHUT_RDWR); close(fd); continue;
    }
    try {
      s->sockets.push_back(fd);
      s->workers.emplace_back([s, fd] { serve_client(s, fd); });
    } catch (...) {
      s->sockets.erase(std::remove(s->sockets.begin(), s->sockets.end(), fd), s->sockets.end());
      shutdown(fd, SHUT_RDWR); close(fd);
    }
  }
}
static int request(nq_pgas_client *c, uint32_t operation, uint32_t index,
                   int64_t a, int64_t b, int64_t *out) {
  if (!c) return -EINVAL;
  std::lock_guard<std::mutex> lock(c->mu);
  if (c->fd < 0) return -ENOTCONN;
  unsigned char req[req_bytes]{};
  put32(req, magic); put32(req + 4, operation); put32(req + 8, index);
  put64(req + 16, static_cast<uint64_t>(a));
  put64(req + 24, static_cast<uint64_t>(b));
  int rc = send_full(c->fd, req, sizeof(req));
  unsigned char response[response_bytes];
  if (!rc) rc = recv_full(c->fd, response, sizeof(response));
  if (rc) { shutdown(c->fd, SHUT_RDWR); close(c->fd); c->fd = -1; return rc; }
  if (get32(response) != magic) return -EPROTO;
  int32_t status = static_cast<int32_t>(get32(response + 4));
  if (status == 0 && out) *out = static_cast<int64_t>(get64(response + 8));
  return status;
}
}

extern "C" {
int nq_pgas_server_start(uint16_t port, const char *token, uint32_t slots,
                         uint32_t participants, nq_pgas_server **out) {
  if (!out || !valid_token(token) || !slots || slots > max_slots || participants > max_clients) return -EINVAL;
  *out = nullptr;
  std::unique_ptr<nq_pgas_server> s(new (std::nothrow) nq_pgas_server);
  if (!s) return -ENOMEM;
  try { s->values.resize(slots, 0); s->token = token; } catch (...) { return -ENOMEM; }
  s->participants = participants;
  s->listener = socket(AF_INET, SOCK_STREAM, 0);
  if (s->listener < 0) return -errno;
  sockaddr_in addr{}; addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (bind(s->listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
      listen(s->listener, static_cast<int>(max_clients)) != 0) {
    int e = errno; close(s->listener); return -e;
  }
  socklen_t size = sizeof(addr);
  if (getsockname(s->listener, reinterpret_cast<sockaddr *>(&addr), &size) != 0) {
    int e = errno; close(s->listener); return -e;
  }
  s->port = ntohs(addr.sin_port);
  try { s->accept_thread = std::thread([p = s.get()] { accept_loop(p); }); }
  catch (...) { close(s->listener); return -EAGAIN; }
  *out = s.release();
  return 0;
}
uint16_t nq_pgas_server_port(nq_pgas_server *s) { return s ? s->port : 0; }
int nq_pgas_server_stop(nq_pgas_server *s) {
  if (!s) return -EINVAL;
  s->stopping = true;
  { std::lock_guard<std::mutex> lock(s->barrier_mu); ++s->generation; s->barrier_result = -ESHUTDOWN; }
  s->barrier_cv.notify_all();
  shutdown(s->listener, SHUT_RDWR); close(s->listener);
  if (s->accept_thread.joinable()) s->accept_thread.join();
  { std::lock_guard<std::mutex> lock(s->clients_mu);
    for (int fd : s->sockets) shutdown(fd, SHUT_RDWR);
  }
  for (auto &t : s->workers) if (t.joinable()) t.join();
  delete s;
  return 0;
}
int nq_pgas_client_connect(const char *host, uint16_t port, const char *token,
                           uint32_t timeout, nq_pgas_client **out) {
  if (!out || !host || !valid_token(token) || !port || !timeout || timeout > max_timeout_ms) return -EINVAL;
  *out = nullptr;
  // Fail closed until authenticated TLS for cross-host access is implemented.
  if (std::strcmp(host, "127.0.0.1") != 0) return -EACCES;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -errno;
  socket_timeout(fd, timeout);
  sockaddr_in addr{}; addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = htons(port);
  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    int e = errno; close(fd); return -e;
  }
  unsigned char length[4]; put32(length, static_cast<uint32_t>(std::strlen(token)));
  int rc = send_full(fd, length, sizeof(length));
  if (!rc) rc = send_full(fd, token, std::strlen(token));
  unsigned char reply[response_bytes];
  if (!rc) rc = recv_full(fd, reply, sizeof(reply));
  if (!rc && get32(reply) != magic) rc = -EPROTO;
  if (!rc) rc = static_cast<int32_t>(get32(reply + 4));
  if (rc) { close(fd); return rc; }
  std::unique_ptr<nq_pgas_client> c(new (std::nothrow) nq_pgas_client);
  if (!c) { close(fd); return -ENOMEM; }
  c->fd = fd; c->timeout_ms = timeout; *out = c.release();
  return 0;
}
int nq_pgas_client_close(nq_pgas_client *c) {
  if (!c) return -EINVAL;
  { std::lock_guard<std::mutex> lock(c->mu);
    if (c->fd >= 0) { shutdown(c->fd, SHUT_RDWR); close(c->fd); c->fd = -1; }
  }
  delete c; return 0;
}
int nq_pgas_ping(nq_pgas_client *c) { return request(c, op_ping, 0, 0, 0, nullptr); }
int nq_pgas_read(nq_pgas_client *c, uint32_t index, int64_t *out) {
  if (!out) return -EINVAL;
  return request(c, op_read, index, 0, 0, out);
}
int nq_pgas_write(nq_pgas_client *c, uint32_t index, int64_t value) {
  return request(c, op_write, index, value, 0, nullptr);
}
int nq_pgas_compare_exchange(nq_pgas_client *c, uint32_t index, int64_t expected,
                             int64_t desired, int64_t *observed) {
  if (!observed) return -EINVAL;
  return request(c, op_cas, index, expected, desired, observed);
}
int nq_pgas_fetch_add(nq_pgas_client *c, uint32_t index, int64_t delta, int64_t *previous) {
  if (!previous) return -EINVAL;
  return request(c, op_add, index, delta, 0, previous);
}
int nq_pgas_barrier(nq_pgas_client *c) { return request(c, op_barrier, 0, 0, 0, nullptr); }
int nq_pgas_location(uint64_t index, uint64_t elements, uint32_t nodes,
                     uint32_t strategy, uint32_t *owner, uint32_t *local) {
  if (!owner || !local || !nodes || !elements || index >= elements || strategy > 1) return -EINVAL;
  uint64_t o, l;
  if (strategy == 1) { o = index % nodes; l = index / nodes; }
  else {
    uint64_t base = elements / nodes, extra = elements % nodes;
    uint64_t bigger = (base + 1) * extra;
    if (index < bigger) { o = index / (base + 1); l = index % (base + 1); }
    else { if (!base) return -ERANGE; o = extra + (index - bigger) / base; l = (index - bigger) % base; }
  }
  if (l > UINT32_MAX) return -EOVERFLOW;
  *owner = static_cast<uint32_t>(o); *local = static_cast<uint32_t>(l);
  return 0;
}
}
