#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/pgas_rdma_control.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>

static nq_pgas_rdma_endpoint endpoint(uint32_t qp,uint32_t key,uint64_t address) {
  nq_pgas_rdma_endpoint e{};
  e.qp_number=qp;e.psn=1234;e.rkey=key;e.slots=16;
  e.address=address;e.lid=1;
  return e;
}
int main(int argc,char** argv){
  assert(argc==8); // shared CA, server cert/key, client cert/key, intruder cert/key
  nq_pgas_rdma_control_server* server=nullptr;
  assert(nq_pgas_rdma_control_server_start("127.0.0.1",0,
    argv[1],argv[2],argv[3],"worker.local",&server)==0);
  const uint16_t port=nq_pgas_rdma_control_server_port(server);
  assert(port>0);
  const nq_pgas_rdma_endpoint serverLocal=endpoint(31,55,0x10001000);
  const nq_pgas_rdma_endpoint clientLocal=endpoint(32,66,0x20002000);
  nq_pgas_rdma_endpoint serverPeer{},clientPeer{};
  int serverResult=-1;
  std::thread first([&]{
    serverResult=nq_pgas_rdma_control_server_exchange(server,&serverLocal,
      &serverPeer,3000);
  });
  int clientResult=nq_pgas_rdma_control_client_exchange("127.0.0.1",port,
    argv[1],argv[4],argv[5],"localhost",&clientLocal,&clientPeer,3000);
  first.join();
  assert(clientResult==0);
  assert(serverResult==0);
  assert(serverPeer.qp_number==clientLocal.qp_number);
  assert(serverPeer.rkey==clientLocal.rkey);
  assert(serverPeer.address==clientLocal.address);
  assert(clientPeer.qp_number==serverLocal.qp_number);
  assert(clientPeer.rkey==serverLocal.rkey);
  assert(clientPeer.address==serverLocal.address);

  // The intruder's cert is signed by the SAME CA, but not allowlisted.
  serverResult=0;serverPeer={};clientPeer={};
  std::thread second([&]{
    serverResult=nq_pgas_rdma_control_server_exchange(server,&serverLocal,
      &serverPeer,3000);
  });
  clientResult=nq_pgas_rdma_control_client_exchange("127.0.0.1",port,
    argv[1],argv[6],argv[7],"localhost",&clientLocal,&clientPeer,3000);
  second.join();
  assert(serverResult<0 && clientResult<0);
  assert(clientPeer.qp_number==0 && serverPeer.qp_number==0);
  assert(nq_pgas_rdma_control_server_stop(server)==0);
  std::puts("PGAS mTLS RDMA capability descriptor exchange and unauthorized peer rejection: PASS");
  return 0;
}
