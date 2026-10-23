#include "noqeri/pgas_runtime.h"
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
  constexpr const char *secret = "shared-secret-for-pgas-process-testing";
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0) {
    close(pipefd[0]);
    nq_pgas_server *server = nullptr;
    if (nq_pgas_server_start(0, secret, 4, 0, &server) != 0) _exit(30);
    uint16_t port = nq_pgas_server_port(server);
    if (write(pipefd[1], &port, sizeof(port)) != sizeof(port)) _exit(31);
    close(pipefd[1]);
    // The server has its own process and is independently addressed over TCP.
    for (;;) pause();
  }
  close(pipefd[1]);
  uint16_t port = 0;
  assert(read(pipefd[0], &port, sizeof(port)) == sizeof(port));
  close(pipefd[0]);
  nq_pgas_client *client = nullptr;
  assert(nq_pgas_client_connect("127.0.0.1", port, secret, 1000, &client) == 0);
  assert(nq_pgas_write(client, 1, 1234) == 0);
  int64_t data = 0;
  assert(nq_pgas_read(client, 1, &data) == 0 && data == 1234);
  assert(nq_pgas_fetch_add(client, 1, 66, &data) == 0 && data == 1234);
  assert(nq_pgas_read(client, 1, &data) == 0 && data == 1300);
  // A crashed node must not silently return stale results.
  assert(kill(child, SIGKILL) == 0);
  int status = 0;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status));
  assert(nq_pgas_ping(client) < 0);
  assert(nq_pgas_client_close(client) == 0);
  std::puts("PGAS cross-process TCP and crash detection: PASS");
}
