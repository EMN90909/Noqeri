#include "noqeri/pgas_runtime.h"
#include "noqeri/pgas_tls_gateway.h"
#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <atomic>

static std::atomic<bool> stopping{false};
static void stop_signal(int) { stopping.store(true); }
static int twice(int64_t in, void*, int64_t* out) {
  *out=in*2;
  return 0;
}
int main(int argc,char** argv) {
  if(argc!=7) return 2; // bind-ip, port, ca, server-crt, server-key, client SAN
  char* end=nullptr;
  long port=strtol(argv[2],&end,10);
  if(!end||*end||port<1||port>65535)return 2;
  const char* token="local-node-token-for-container-test";
  nq_pgas_server* node=nullptr;
  int status=nq_pgas_server_start(0,token,32,0,&node);
  if(status){std::fprintf(stderr,"node: %d\n",status);return 3;}
  status=nq_pgas_register_task(node,8,twice,nullptr);
  if(status)return 4;
  nq_pgas_tls_gateway* gateway=nullptr;
  status=nq_pgas_tls_gateway_start(argv[1],static_cast<uint16_t>(port),
    nq_pgas_server_port(node),token,argv[3],argv[4],argv[5],argv[6],&gateway);
  if(status){std::fprintf(stderr,"gateway: %d\n",status);nq_pgas_server_stop(node);return 5;}
  std::signal(SIGTERM,stop_signal); std::signal(SIGINT,stop_signal);
  std::puts("PGAS gateway online");std::fflush(stdout);
  while(!stopping.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  nq_pgas_tls_gateway_stop(gateway);
  nq_pgas_server_stop(node);
  return 0;
}
