#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/pgas_runtime.h"
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static constexpr const char *secret = "private-pgas-durable-test-token";
int main() {
  char tmpl[] = "/tmp/noqeri-pgas-wal-XXXXXX";
  char *dir = mkdtemp(tmpl);
  assert(dir);
  const std::string path = std::string(dir) + "/node.wal";
  int pipefd[2]; assert(pipe(pipefd) == 0);
  pid_t child = fork(); assert(child >= 0);
  if (child == 0) {
    close(pipefd[0]);
    nq_pgas_server *server = nullptr;
    if (nq_pgas_server_start_durable(0, secret, 8, 0, path.c_str(), &server) != 0) _exit(11);
    uint16_t port = nq_pgas_server_port(server);
    if (write(pipefd[1], &port, sizeof(port)) != sizeof(port)) _exit(12);
    close(pipefd[1]);
    for (;;) pause();
  }
  close(pipefd[1]);
  uint16_t port = 0;
  assert(read(pipefd[0], &port, sizeof(port)) == sizeof(port));
  close(pipefd[0]);
  nq_pgas_client *client = nullptr;
  assert(nq_pgas_client_connect("127.0.0.1", port, secret, 3000, &client) == 0);
  assert(nq_pgas_write(client, 1, 101) == 0);
  int64_t previous = 0;
  assert(nq_pgas_fetch_add(client, 1, 33, &previous) == 0 && previous == 101);
  assert(nq_pgas_compare_exchange(client, 1, 134, 999, &previous) == 0 && previous == 134);
  assert(nq_pgas_write(client, 7, -991) == 0);
  // Another writer must not use an already exclusively locked WAL.
  nq_pgas_server *locked = nullptr;
  assert(nq_pgas_server_start_durable(0, secret, 8, 0, path.c_str(), &locked) < 0);
  assert(locked == nullptr);
  // Kill the owner process without clean shutdown.
  assert(kill(child, SIGKILL) == 0);
  int status=0; assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status));
  assert(nq_pgas_ping(client) < 0);
  assert(nq_pgas_client_close(client) == 0);
  nq_pgas_server *restored = nullptr;
  assert(nq_pgas_server_start_durable(0, secret, 8, 0, path.c_str(), &restored) == 0);
  client = nullptr;
  assert(nq_pgas_client_connect("127.0.0.1", nq_pgas_server_port(restored), secret, 3000, &client) == 0);
  int64_t value = 0;
  assert(nq_pgas_read(client, 1, &value) == 0 && value == 999);
  assert(nq_pgas_read(client, 7, &value) == 0 && value == -991);
  assert(nq_pgas_client_close(client) == 0);
  assert(nq_pgas_server_stop(restored) == 0);

  // An incomplete final record is not accepted as a committed operation.
  int fd=open(path.c_str(), O_WRONLY | O_APPEND);
  assert(fd >= 0);
  const unsigned char partial[3]={1,2,3};
  assert(write(fd,partial,sizeof(partial)) == sizeof(partial));
  close(fd);
  assert(nq_pgas_server_start_durable(0,secret,8,0,path.c_str(),&restored) == 0);
  assert(nq_pgas_server_stop(restored) == 0);
  struct stat st{}; assert(stat(path.c_str(), &st) == 0);
  assert(st.st_size == 16 + 4*32);

  // A valid-length but corrupt record cannot be silently replayed.
  fd = open(path.c_str(), O_RDWR);
  assert(fd >= 0);
  assert(lseek(fd, 16 + 3*32 + 24, SEEK_SET) >= 0);
  unsigned char garbage=0xFF;
  assert(write(fd,&garbage,1) == 1);
  assert(fsync(fd) == 0); close(fd);
  restored = nullptr;
  assert(nq_pgas_server_start_durable(0,secret,8,0,path.c_str(),&restored) == -EBADMSG);
  assert(restored == nullptr);
  assert(unlink(path.c_str()) == 0);
  assert(rmdir(dir) == 0);
  std::puts("PGAS durable replay, crash, exclusive ownership, torn-tail, and corruption: PASS");
}
