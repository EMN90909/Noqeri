#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include "noqeri/mailbox_extended.h"
int main(int argc,char**argv){
  assert(argc==4);
  auto*server=nq_mail_tls_server(argv[1],argv[2]);assert(server);
  auto*client=nq_mail_tls_client(argv[3]);assert(client);
  int s[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);
  assert(nq_mail_set_nonblocking(s[0])==0&&nq_mail_set_nonblocking(s[1])==0);
  auto*a=nq_mail_tls_attach(server,s[0],nullptr);
  auto*b=nq_mail_tls_attach(client,s[1],"localhost");assert(a&&b);
  bool ready_a=false,ready_b=false;
  for(int i=0;i<1000&&(!ready_a||!ready_b);++i){
    if(!ready_a){int r=nq_mail_tls_handshake(a);assert(r==0||r==-EAGAIN);ready_a=r==0;}
    if(!ready_b){int r=nq_mail_tls_handshake(b);assert(r==0||r==-EAGAIN);ready_b=r==0;}
  }
  assert(ready_a&&ready_b);
  const char msg[]="secure";
  assert(nq_mail_tls_write(b,msg,6)==6);
  char out[16]{};
  int64_t r=-EAGAIN;
  for(int i=0;i<100&&r==-EAGAIN;++i)r=nq_mail_tls_read(a,out,sizeof(out));
  assert(r==6&&std::memcmp(out,msg,6)==0);
  nq_mail_tls_session_destroy(a);nq_mail_tls_session_destroy(b);
  nq_mail_tls_context_destroy(server);nq_mail_tls_context_destroy(client);
  close(s[0]);close(s[1]);
}
