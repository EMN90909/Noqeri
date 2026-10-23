#include "noqeri/pgas_tls_gateway.h"
#include <netdb.h>
#include <arpa/inet.h>
#include <cassert>
#include <cstdlib>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

int main(int argc,char** argv) {
  if(argc!=8)return 2; // host, port, ca, cert, key, expected server dns, expected client dns (doc)
  addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
  addrinfo* address=nullptr;
  if(getaddrinfo(argv[1],nullptr,&hints,&address)!=0)return 3;
  char ip[INET_ADDRSTRLEN]{};
  auto* a=reinterpret_cast<sockaddr_in*>(address->ai_addr);
  bool ok=inet_ntop(AF_INET,&a->sin_addr,ip,sizeof(ip))!=nullptr;
  freeaddrinfo(address);
  if(!ok)return 4;
  char* end=nullptr;long port=strtol(argv[2],&end,10);
  if(!end||*end||port<1||port>65535)return 2;
  nq_pgas_tls_client* client=nullptr;
  int status=nq_pgas_tls_client_connect(ip,static_cast<uint16_t>(port),argv[3],
    argv[4],argv[5],argv[6],4000,&client);
  if(status){std::fprintf(stderr,"TLS connect failed %d\n",status);return 5;}
  if(nq_pgas_tls_ping(client)!=0)return 6;
  if(nq_pgas_tls_write(client,4,99)!=0)return 7;
  int64_t result=0;
  if(nq_pgas_tls_read(client,4,&result)!=0 || result!=99)return 8;
  if(nq_pgas_tls_fetch_add(client,4,10,&result)!=0 || result!=99)return 9;
  if(nq_pgas_tls_run_task(client,8,21,&result)!=0 || result!=42)return 10;
  if(nq_pgas_tls_client_close(client)!=0)return 11;
  std::puts("PGAS isolated-container encrypted RPC: PASS");
  return 0;
}
