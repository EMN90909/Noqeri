#include "noqeri/pgas_runtime.h"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

static constexpr const char *token = "a-demo-long-shared-secret-not-for-production";
static nq_pgas_client *dial(uint16_t port) {
  nq_pgas_client *c = nullptr;
  assert(nq_pgas_client_connect("127.0.0.1", port, token, 6000, &c) == 0);
  assert(c); return c;
}
static int multiply_three(int64_t in, void *, int64_t *out) {
  if (!out) return -EINVAL;
  *out = in * 3;
  return 0;
}
int main() {
  uint32_t owner = 99, local = 99;
  for (uint32_t n = 1; n <= 10; ++n) {
    for (uint32_t size = 1; size <= 100; ++size) {
      for (uint32_t strategy = 0; strategy < 2; ++strategy) {
        std::vector<uint32_t> counts(n, 0);
        for (uint32_t i = 0; i < size; ++i) {
          assert(nq_pgas_location(i, size, n, strategy, &owner, &local) == 0);
          assert(owner < n && local == counts[owner]++);
        }
      }
    }
  }
  assert(nq_pgas_location(0, 0, 2, 0, &owner, &local) == -EINVAL);
  assert(nq_pgas_location(1, 1, 1, 0, &owner, &local) == -EINVAL);
  assert(nq_pgas_location(0, 1, 1, 4, &owner, &local) == -EINVAL);
  nq_pgas_server *s0 = nullptr, *s1 = nullptr;
  assert(nq_pgas_server_start(0, token, 16, 2, &s0) == 0);
  assert(nq_pgas_server_start(0, token, 16, 0, &s1) == 0);
  assert(nq_pgas_register_task(s0, 7, multiply_three, nullptr) == 0);
  assert(nq_pgas_register_task(s0, 7, multiply_three, nullptr) == -EEXIST);
  uint16_t p0 = nq_pgas_server_port(s0), p1 = nq_pgas_server_port(s1);
  assert(p0 && p1);
  nq_pgas_client *c0 = dial(p0), *c1 = dial(p1);
  assert(nq_pgas_ping(c0) == 0 && nq_pgas_ping(c1) == 0);
  int64_t remote_result = 0;
  assert(nq_pgas_run_task(c0, 7, 21, &remote_result) == 0 && remote_result == 63);
  assert(nq_pgas_run_task(c1, 7, 21, &remote_result) == -ENOENT);
  const uint8_t script[] = {1,2,3,5,2,5,3,14}; // input*3+5
  assert(nq_pgas_execute_program(c0, script, sizeof(script), 7, 32, &remote_result) == 0);
  assert(remote_result == 26);
  const uint8_t divide_zero[] = {1,15,6,14};
  assert(nq_pgas_execute_program(c0, divide_zero, sizeof(divide_zero), 7, 32, &remote_result) == -EDOM);
  const uint8_t invalid_program[] = {255};
  assert(nq_pgas_execute_program(c0, invalid_program, 1, 7, 32, &remote_result) == -ENOTSUP);
  const uint8_t infinite_loop[] = {10,254};
  assert(nq_pgas_execute_program(c0, infinite_loop, 2, 7, 8, &remote_result) == -ETIMEDOUT);
  const uint8_t underflow[] = {3,14};
  assert(nq_pgas_execute_program(c0, underflow, 2, 7, 32, &remote_result) == -EBADMSG);
  assert(nq_pgas_execute_program(c0, script, sizeof(script), 7, 0, &remote_result) == -EINVAL);
  assert(nq_pgas_write(c0, 0, 12) == 0);
  assert(nq_pgas_write(c1, 0, 73) == 0);
  int64_t value = 0;
  assert(nq_pgas_read(c0, 0, &value) == 0 && value == 12);
  assert(nq_pgas_read(c1, 0, &value) == 0 && value == 73);
  assert(nq_pgas_compare_exchange(c0, 0, 4, 30, &value) == 0 && value == 12);
  assert(nq_pgas_compare_exchange(c0, 0, 12, 30, &value) == 0 && value == 12);
  assert(nq_pgas_fetch_add(c0, 0, 5, &value) == 0 && value == 30);
  assert(nq_pgas_read(c0, 0, &value) == 0 && value == 35);
  assert(nq_pgas_read(c0, 16, &value) == -ERANGE);
  assert(nq_pgas_barrier(c1) == -ENOTSUP);
  nq_pgas_client *invalid = nullptr;
  assert(nq_pgas_client_connect("10.0.0.1", p0, token, 1000, &invalid) == -EACCES);
  assert(nq_pgas_client_connect("127.0.0.1", p0, "incorrect-test-token", 1000, &invalid) == -EACCES);
  nq_pgas_client *barrier_peer = dial(p0);
  std::thread b([&] { assert(nq_pgas_barrier(barrier_peer) == 0); });
  assert(nq_pgas_barrier(c0) == 0); b.join();
  assert(nq_pgas_client_close(barrier_peer) == 0);
  std::vector<std::thread> workers;
  for (int i = 0; i < 8; ++i) workers.emplace_back([&,i] {
    nq_pgas_client *c = dial(p1);
    for (int j = 0; j < 100; ++j) {
      int64_t before;
      assert(nq_pgas_fetch_add(c, 2, 1, &before) == 0);
    }
    assert(nq_pgas_client_close(c) == 0);
  });
  for (auto &t : workers) t.join();
  assert(nq_pgas_read(c1, 2, &value) == 0 && value == 800);
  assert(nq_pgas_client_close(c0) == 0);
  assert(nq_pgas_client_close(c1) == 0);
  assert(nq_pgas_server_stop(s0) == 0);
  assert(nq_pgas_server_stop(s1) == 0);
  std::puts("PGAS native loopback transport and partition mapping: PASS");
}
