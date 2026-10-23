#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/pgas_runtime.h"
#include "noqeri/pgas_tls_gateway.h"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

int main(int argc,char **argv) {
  assert(argc==6);
  const char *ca=argv[1], *server_cert=argv[2], *server_key=argv[3];
  const char *client_cert=argv[4],*client_key=argv[5];
  const char *token="local-backend-protected-token-2026";
  nq_pgas_server *node=nullptr;
  assert(nq_pgas_server_start(0,token,16,0,&node)==0);
  nq_pgas_tls_gateway *gateway=nullptr;
  assert(nq_pgas_tls_gateway_start("127.0.0.1",0,nq_pgas_server_port(node),
    token,ca,server_cert,server_key,"worker.local",&gateway)==0);
  assert(nq_pgas_tls_gateway_port(gateway)!=0);
  nq_pgas_tls_client *remote=nullptr;
  const uint16_t port=nq_pgas_tls_gateway_port(gateway);
  assert(nq_pgas_tls_client_connect("127.0.0.1",port,ca,client_cert,client_key,
    "localhost",3000,&remote)==0);
  assert(nq_pgas_tls_ping(remote)==0);
  assert(nq_pgas_tls_write(remote,0,1001)==0);
  int64_t value=0;
  assert(nq_pgas_tls_read(remote,0,&value)==0 && value==1001);
  assert(nq_pgas_tls_compare_exchange(remote,0,1001,10,&value)==0 && value==1001);
  assert(nq_pgas_tls_fetch_add(remote,0,8,&value)==0 && value==10);
  assert(nq_pgas_tls_read(remote,0,&value)==0 && value==18);
  assert(nq_pgas_tls_read(remote,16,&value)==-ERANGE);
  assert(nq_pgas_tls_client_close(remote)==0);
  remote=nullptr;
  // Hostname verification is enforced, even with a trusted CA.
  assert(nq_pgas_tls_client_connect("127.0.0.1",port,ca,client_cert,client_key,
    "not-localhost.example",3000,&remote)<0);
  assert(remote==nullptr);
  assert(nq_pgas_tls_gateway_stop(gateway)==0);
  assert(nq_pgas_server_stop(node)==0);
  std::puts("PGAS TLS1.3 mTLS encrypted RPC and hostname rejection: PASS");
}
