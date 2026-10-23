#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/pgas_runtime.h"
#include "noqeri/pgas_partition.h"
#include "noqeri/pgas_tls_gateway.h"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>

extern "C" int noqeri_entry(const void* abi);
int main(int argc,char **argv) {
  assert(argc==8); // existing ephemeral mTLS-cert fixture
  const char *token="noqeri-pgas-tls-backend-secret-2026";
  nq_pgas_server *nodes[2]{};
  nq_pgas_client *observers[2]{};
  nq_pgas_tls_gateway *gateways[2]{};
  nq_pgas_tls_client *tls[2]{};
  for(int i=0;i<2;++i) {
    assert(nq_pgas_server_start(0,token,3,0,&nodes[i])==0);
    assert(nq_pgas_client_connect("127.0.0.1",nq_pgas_server_port(nodes[i]),token,3000,&observers[i])==0);
    assert(nq_pgas_tls_gateway_start("127.0.0.1",0,nq_pgas_server_port(nodes[i]),
      token,argv[1],argv[2],argv[3],"worker.local",&gateways[i])==0);
    assert(nq_pgas_tls_client_connect("127.0.0.1",nq_pgas_tls_gateway_port(gateways[i]),
      argv[1],argv[4],argv[5],"localhost",3000,&tls[i])==0);
  }
  // The compiler must use the configured TLS topology and must not
  // quietly redirect failed reads/writes to the local node.
  assert(nq_pgas_partition_bind_tls("dataset",6,2,1,tls)==0);
  assert(noqeri_entry(nullptr)==0);
  int64_t result=0;
  assert(nq_pgas_read(observers[0],0,&result)==0 && result==101);
  assert(nq_pgas_read(observers[1],0,&result)==0 && result==202);
  assert(nq_pgas_read(observers[0],1,&result)==0 && result==106);
  assert(nq_pgas_partition_unbind("dataset")==0);
  for(int i=0;i<2;++i) {
    assert(nq_pgas_tls_client_close(tls[i])==0);
    assert(nq_pgas_tls_gateway_stop(gateways[i])==0);
    assert(nq_pgas_client_close(observers[i])==0);
    assert(nq_pgas_server_stop(nodes[i])==0);
  }
  std::puts("PGAS native Noqeri compiled arrays over two independent mTLS node gateways: PASS");
  return 0;
}
