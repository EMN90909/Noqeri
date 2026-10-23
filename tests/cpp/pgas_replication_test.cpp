#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/pgas_runtime.h"
#include "noqeri/pgas_tls_gateway.h"
#include "noqeri/pgas_replication.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <unistd.h>

int main(int argc,char **argv) {
  assert(argc==6);
  char name[]="/tmp/noqeri-pgas-pair-XXXXXX";
  char *directory=mkdtemp(name);assert(directory);
  std::string first=std::string(directory)+"/primary.wal";
  std::string second=std::string(directory)+"/standby.wal";
  const char *token="two-node-replica-test-token-2026";
  nq_pgas_server *a=nullptr,*b=nullptr;
  assert(nq_pgas_server_start_durable(0,token,8,0,first.c_str(),&a)==0);
  assert(nq_pgas_server_start_durable(0,token,8,0,second.c_str(),&b)==0);
  nq_pgas_tls_gateway *ga=nullptr,*gb=nullptr;
  assert(nq_pgas_tls_gateway_start("127.0.0.1",0,nq_pgas_server_port(a),
         token,argv[1],argv[2],argv[3],"worker.local",&ga)==0);
  assert(nq_pgas_tls_gateway_start("127.0.0.1",0,nq_pgas_server_port(b),
         token,argv[1],argv[2],argv[3],"worker.local",&gb)==0);
  nq_pgas_tls_client *ca=nullptr,*cb=nullptr;
  assert(nq_pgas_tls_client_connect("127.0.0.1",nq_pgas_tls_gateway_port(ga),
         argv[1],argv[4],argv[5],"localhost",3000,&ca)==0);
  assert(nq_pgas_tls_client_connect("127.0.0.1",nq_pgas_tls_gateway_port(gb),
         argv[1],argv[4],argv[5],"localhost",3000,&cb)==0);
  nq_pgas_replica_pair *pair=nullptr;
  assert(nq_pgas_replica_pair_create(ca,cb,&pair)==0);
  assert(nq_pgas_replica_pair_write(pair,0,100)==0);
  assert(nq_pgas_replica_pair_write(pair,1,-20)==0);
  int64_t value=0;
  assert(nq_pgas_replica_pair_read_verified(pair,0,&value)==0&&value==100);
  assert(nq_pgas_replica_pair_read_verified(pair,1,&value)==0&&value==-20);
  // Simulate operator-confirmed leader shutdown, NOT split-brain discovery.
  assert(nq_pgas_tls_gateway_stop(ga)==0);
  assert(nq_pgas_server_stop(a)==0);
  assert(nq_pgas_replica_pair_read_verified(pair,0,&value)<0);
  assert(nq_pgas_replica_pair_write(pair,0,400)==-EUCLEAN);
  assert(nq_pgas_replica_pair_close(pair)==0);
  assert(nq_pgas_tls_client_close(ca)==0);
  // External fencing of the old leader was guaranteed above (shutdown).
  assert(nq_pgas_server_promote_epoch(b,2)==0);
  assert(nq_pgas_tls_read(cb,0,&value)==-ESTALE);
  assert(nq_pgas_tls_client_set_epoch(cb,2)==0);
  assert(nq_pgas_tls_read(cb,0,&value)==0&&value==100);
  assert(nq_pgas_tls_read(cb,1,&value)==0&&value==-20);
  assert(nq_pgas_tls_client_close(cb)==0);
  assert(nq_pgas_tls_gateway_stop(gb)==0);
  assert(nq_pgas_server_stop(b)==0);
  b=nullptr;
  // New process/node instance replays acknowledged standby WAL after promotion.
  assert(nq_pgas_server_start_durable(0,token,8,0,second.c_str(),&b)==0);
  uint32_t epoch=0;
  assert(nq_pgas_server_get_epoch(b,&epoch)==0&&epoch==2);
  assert(nq_pgas_server_stop(b)==0);
  assert(unlink(first.c_str())==0);
  assert(unlink(second.c_str())==0);
  assert(rmdir(directory)==0);
  std::puts("PGAS synchronous replica acknowledgement, manual promotion, and fenced WAL recovery: PASS");
  return 0;
}
