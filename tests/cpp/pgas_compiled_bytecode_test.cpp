#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/pgas_runtime.h"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc,char**argv) {
  assert(argc==2);
  std::ifstream input(argv[1],std::ios::binary);
  assert(input);
  std::vector<uint8_t> bytes(std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{});
  assert(!bytes.empty() && bytes.size()<=256);
  const char* secret="noqeri-captured-bytecode-secret-2026";
  nq_pgas_server* node=nullptr;
  assert(nq_pgas_server_start(0,secret,4,0,&node)==0);
  nq_pgas_client* remote=nullptr;
  assert(nq_pgas_client_connect("127.0.0.1",nq_pgas_server_port(node),secret,3000,&remote)==0);
  int64_t result=0;
  assert(nq_pgas_execute_program(remote,bytes.data(),bytes.size(),37,4096,&result)==0);
  assert(result==42);
  assert(nq_pgas_execute_program(remote,bytes.data(),bytes.size(),-5,4096,&result)==0);
  assert(result==0);
  assert(nq_pgas_execute_program(remote,bytes.data(),bytes.size(),42,1,&result)==-ETIMEDOUT);
  assert(nq_pgas_client_close(remote)==0);
  assert(nq_pgas_server_stop(node)==0);
  std::puts("PGAS compiled pure Noqeri closure executed in isolated arithmetic VM: PASS");
}
