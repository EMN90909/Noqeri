#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/pgas_runtime.h"
#include "noqeri/pgas_partition.h"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>

extern "C" int noqeri_entry(const void* abi);
int main() {
  const char *secret="compiled-pgas-abi-fixture-secret-2026";
  nq_pgas_server *nodes[2]{};
  assert(nq_pgas_server_start(0,secret,3,0,&nodes[0])==0);
  assert(nq_pgas_server_start(0,secret,3,0,&nodes[1])==0);
  nq_pgas_client *clients[2]{};
  for(int i=0;i<2;++i)
    assert(nq_pgas_client_connect("127.0.0.1",nq_pgas_server_port(nodes[i]),
                                  secret,3000,&clients[i])==0);
  assert(nq_pgas_partition_bind("dataset",6,2,1,clients)==0);
  assert(nq_pgas_partition_bind("dataset",6,2,1,clients)==-EEXIST);
  assert(noqeri_entry(nullptr)==0);
  int64_t value=0;
  assert(nq_pgas_read(clients[0],0,&value)==0 && value==101);
  assert(nq_pgas_read(clients[1],0,&value)==0 && value==202);
  assert(nq_pgas_read(clients[0],1,&value)==0 && value==106);
  assert(nq_pgas_partition_unbind("dataset")==0);
  assert(nq_pgas_partition_unbind("dataset")==-ENOENT);
  for(int i=0;i<2;++i) assert(nq_pgas_client_close(clients[i])==0);
  for(int i=0;i<2;++i) assert(nq_pgas_server_stop(nodes[i])==0);
  std::puts("PGAS compiled Noqeri array indexed reads and writes: PASS");
}
