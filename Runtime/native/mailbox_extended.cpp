#include "noqeri/mailbox_extended.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <new>
#include <string>
#include <vector>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
struct nq_mail_reactor { int epfd; };
static uint32_t to_epoll(uint32_t e) {
  uint32_t x=0;if(e&1)x|=EPOLLIN;if(e&2)x|=EPOLLOUT;return x;
}
extern "C" {
nq_mail_reactor* nq_mail_reactor_create() {
  int fd=epoll_create1(EPOLL_CLOEXEC);if(fd<0)return nullptr;
  auto*p=new(std::nothrow)nq_mail_reactor{fd};if(!p)close(fd);return p;
}
static int ctl(nq_mail_reactor*p,int op,int fd,uint32_t events) {
  if(!p||fd<0||(events&~3u)||((op!=EPOLL_CTL_DEL)&&!events))return -EINVAL;
  epoll_event ev{};ev.events=to_epoll(events);ev.data.fd=fd;
  if(epoll_ctl(p->epfd,op,fd,op==EPOLL_CTL_DEL?nullptr:&ev)<0)return -errno;
  return 0;
}
int nq_mail_reactor_add(nq_mail_reactor*p,int fd,uint32_t e){return ctl(p,EPOLL_CTL_ADD,fd,e);}
int nq_mail_reactor_modify(nq_mail_reactor*p,int fd,uint32_t e){return ctl(p,EPOLL_CTL_MOD,fd,e);}
int nq_mail_reactor_remove(nq_mail_reactor*p,int fd){return ctl(p,EPOLL_CTL_DEL,fd,0);}
int nq_mail_reactor_wait(nq_mail_reactor*p,nq_mail_event*out,int cap,int timeout) {
  if(!p||!out||cap<=0||cap>65536||timeout< -1)return -EINVAL;
  std::vector<epoll_event> events(static_cast<size_t>(cap));
  int n;do{n=epoll_wait(p->epfd,events.data(),cap,timeout);}while(n<0&&errno==EINTR);
  if(n<0)return -errno;
  for(int i=0;i<n;++i){
    out[i].fd=events[i].data.fd;out[i].events=0;
    if(events[i].events&EPOLLIN)out[i].events|=1;
    if(events[i].events&EPOLLOUT)out[i].events|=2;
    if(events[i].events&(EPOLLERR|EPOLLHUP|EPOLLRDHUP))out[i].events|=4;
  }
  return n;
}
void nq_mail_reactor_destroy(nq_mail_reactor*p){if(p){close(p->epfd);delete p;}}
int nq_mail_set_nonblocking(int fd){
  int f=fcntl(fd,F_GETFL);if(f<0)return -errno;
  return fcntl(fd,F_SETFL,f|O_NONBLOCK)<0?-errno:0;
}
}
struct nq_mail_tls_context { SSL_CTX* ctx; bool server; };
struct nq_mail_tls_session { SSL* ssl; int wanted; };
static int tls_error(nq_mail_tls_session*p,int result){
  int e=SSL_get_error(p->ssl,result);
  if(e==SSL_ERROR_WANT_READ){p->wanted=1;return -EAGAIN;}
  if(e==SSL_ERROR_WANT_WRITE){p->wanted=2;return -EAGAIN;}
  if(e==SSL_ERROR_ZERO_RETURN){p->wanted=0;return -ECONNRESET;}
  p->wanted=0;return -EPROTO;
}
extern "C" {
nq_mail_tls_context* nq_mail_tls_server(const char* cert,const char* key){
  if(!cert||!key)return nullptr;
  SSL_CTX*ctx=SSL_CTX_new(TLS_server_method());if(!ctx)return nullptr;
  if(SSL_CTX_set_min_proto_version(ctx,TLS1_2_VERSION)!=1||
     SSL_CTX_use_certificate_chain_file(ctx,cert)!=1||
     SSL_CTX_use_PrivateKey_file(ctx,key,SSL_FILETYPE_PEM)!=1||
     SSL_CTX_check_private_key(ctx)!=1){SSL_CTX_free(ctx);return nullptr;}
  auto*p=new(std::nothrow)nq_mail_tls_context{ctx,true};if(!p)SSL_CTX_free(ctx);return p;
}
nq_mail_tls_context* nq_mail_tls_client(const char* ca_file){
  SSL_CTX*ctx=SSL_CTX_new(TLS_client_method());if(!ctx)return nullptr;
  SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,nullptr);
  if(SSL_CTX_set_min_proto_version(ctx,TLS1_2_VERSION)!=1||
     (ca_file?SSL_CTX_load_verify_locations(ctx,ca_file,nullptr)!=1:SSL_CTX_set_default_verify_paths(ctx)!=1)){
    SSL_CTX_free(ctx);return nullptr;
  }
  auto*p=new(std::nothrow)nq_mail_tls_context{ctx,false};if(!p)SSL_CTX_free(ctx);return p;
}
nq_mail_tls_session* nq_mail_tls_attach(nq_mail_tls_context*p,int fd,const char*host){
  if(!p||fd<0||(!p->server&&(!host||!*host)))return nullptr;
  SSL*s=SSL_new(p->ctx);if(!s)return nullptr;
  if(SSL_set_fd(s,fd)!=1){SSL_free(s);return nullptr;}
  if(p->server)SSL_set_accept_state(s);
  else {
    if(SSL_set_tlsext_host_name(s,host)!=1||
       SSL_set1_host(s,host)!=1){SSL_free(s);return nullptr;}
    SSL_set_connect_state(s);
  }
  auto*session=new(std::nothrow)nq_mail_tls_session{s,0};
  if(!session)SSL_free(s);return session;
}
int nq_mail_tls_handshake(nq_mail_tls_session*p){
  if(!p)return -EINVAL;
  int r=SSL_do_handshake(p->ssl);
  if(r==1){p->wanted=0;return 0;}
  return tls_error(p,r);
}
int64_t nq_mail_tls_read(nq_mail_tls_session*p,void*buf,size_t size){
  if(!p||(!buf&&size))return -EINVAL;
  if(size==0)return 0;
  int r=SSL_read(p->ssl,buf,static_cast<int>(std::min(size,static_cast<size_t>(INT32_MAX))));
  if(r>0){p->wanted=0;return r;}return tls_error(p,r);
}
int64_t nq_mail_tls_write(nq_mail_tls_session*p,const void*buf,size_t size){
  if(!p||(!buf&&size))return -EINVAL;
  if(size==0)return 0;
  int r=SSL_write(p->ssl,buf,static_cast<int>(std::min(size,static_cast<size_t>(INT32_MAX))));
  if(r>0){p->wanted=0;return r;}return tls_error(p,r);
}
int nq_mail_tls_want_events(nq_mail_tls_session*p){return p?p->wanted:-EINVAL;}
void nq_mail_tls_session_destroy(nq_mail_tls_session*p){if(p){SSL_free(p->ssl);delete p;}}
void nq_mail_tls_context_destroy(nq_mail_tls_context*p){if(p){SSL_CTX_free(p->ctx);delete p;}}
}
static bool valid_id(const char*id){
  if(!id||!*id)return false;
  size_t n=0;
  for(const unsigned char*c=reinterpret_cast<const unsigned char*>(id);*c;++c){
    if(++n>128)return false;
    if(!((*c>='a'&&*c<='z')||(*c>='A'&&*c<='Z')||
         (*c>='0'&&*c<='9')||*c=='-'||*c=='_'))return false;
  }
  return true;
}
extern "C" int nq_mail_commit_message(const char*directory,const char*id,const void*data,size_t size){
  if(!directory||!valid_id(id)||(!data&&size))return -EINVAL;
  int dir=open(directory,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(dir<0)return -errno;
  static std::atomic<uint64_t> seq{0};
  std::string final=std::string(id)+".msg";
  std::string temp=".pending-"+std::to_string(getpid())+"-"+std::to_string(seq.fetch_add(1));
  int fd=openat(dir,temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
  if(fd<0){int e=errno;close(dir);return -e;}
  int error=0;
  size_t off=0;
  while(off<size){
    ssize_t n=write(fd,static_cast<const char*>(data)+off,size-off);
    if(n<0&&errno==EINTR)continue;
    if(n<=0){error=n<0?errno:EIO;break;}
    off+=static_cast<size_t>(n);
  }
  if(!error&&fsync(fd)<0)error=errno;
  if(close(fd)<0&&!error)error=errno;
  // A hard-link is an atomic NO-REPLACE publication, even with racing writers.
  if(!error&&linkat(dir,temp.c_str(),dir,final.c_str(),0)<0)error=errno;
  if(!error&&fsync(dir)<0)error=errno;
  unlinkat(dir,temp.c_str(),0);
  // Persist temporary name cleanup when possible. If this fails after the
  // durable publish, caller sees an error and must deduplicate by message ID.
  if(!error&&fsync(dir)<0)error=errno;
  close(dir);
  return error?-error:0;
}
