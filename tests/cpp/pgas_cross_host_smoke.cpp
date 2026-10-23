#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/pgas_runtime.h"
#include "noqeri/pgas_tls_gateway.h"
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>

static std::atomic<bool> running{true};
static void stop(int) { running.store(false); }
static void usage(){
  std::fprintf(stderr,
    "server <bindIPv4> <tls-port> <wal-absolute-path> <token> <ca.pem> <server.crt> <server.key> <client-san-dns>\n"
    "client <serverIPv4> <tls-port> <ca.pem> <client.crt> <client.key> <server-san-dns>\n");
}
static uint16_t parse_port(const char *v){
  if(!v||!*v)return 0;
  char *end=nullptr;long n=std::strtol(v,&end,10);
  return end&&!*end&&n>0&&n<=65535?static_cast<uint16_t>(n):0;
}
int main(int argc,char **argv){
  if(argc>=2 && std::string(argv[1])=="server" && argc==10) {
    const uint16_t port=parse_port(argv[3]);
    if(!port){usage();return 2;}
    nq_pgas_server *node=nullptr;
    int rc=nq_pgas_server_start_durable(0,argv[5],8,0,argv[4],&node);
    if(rc){std::fprintf(stderr,"durable node start failed: %d\n",rc);return 1;}
    nq_pgas_tls_gateway *gateway=nullptr;
    rc=nq_pgas_tls_gateway_start(argv[2],port,nq_pgas_server_port(node),
        argv[5],argv[6],argv[7],argv[8],argv[9],&gateway);
    if(rc){
      std::fprintf(stderr,"TLS gateway start failed: %d\n",rc);
      nq_pgas_server_stop(node);return 1;
    }
    std::signal(SIGINT,stop);
    std::signal(SIGTERM,stop);
    std::printf("PGAS_TLS_READY %s %u\n",argv[2],nq_pgas_tls_gateway_port(gateway));
    std::fflush(stdout);
    while(running.load())std::this_thread::sleep_for(std::chrono::milliseconds(100));
    nq_pgas_tls_gateway_stop(gateway);
    nq_pgas_server_stop(node);
    return 0;
  }
  if(argc>=2 && std::string(argv[1])=="client" && argc==8) {
    const uint16_t port=parse_port(argv[3]);
    if(!port){usage();return 2;}
    nq_pgas_tls_client *client=nullptr;
    int rc=nq_pgas_tls_client_connect(argv[2],port,argv[4],argv[5],argv[6],
                                      argv[7],3000,&client);
    if(rc){std::fprintf(stderr,"mTLS connect failed: %d\n",rc);return 1;}
    rc=nq_pgas_tls_ping(client);
    // Dedicated disposable test node only. Never target production data.
    if(!rc)rc=nq_pgas_tls_write(client,0,424242);
    int64_t read_back=0;
    if(!rc)rc=nq_pgas_tls_read(client,0,&read_back);
    if(!rc && read_back!=424242)rc=-1;
    nq_pgas_tls_client_close(client);
    if(rc){std::fprintf(stderr,"mTLS remote PGAS round-trip failed: %d\n",rc);return 1;}
    std::puts("PGAS_CROSS_HOST_MTLS_PING_READ_WRITE_PASS");
    return 0;
  }
  usage();return 2;
}
