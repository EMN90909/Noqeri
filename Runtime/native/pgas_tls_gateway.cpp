#include "noqeri/pgas_tls_gateway.h"
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

namespace {
constexpr uint32_t magic=0x4e515047;
constexpr unsigned char max_sessions=64;
static uint32_t get32(const unsigned char *p){uint32_t n;std::memcpy(&n,p,4);return ntohl(n);}
static void put32(unsigned char *p,uint32_t n){n=htonl(n);std::memcpy(p,&n,4);}
static uint64_t get64(const unsigned char *p){return (uint64_t(get32(p))<<32)|get32(p+4);}
static void put64(unsigned char *p,uint64_t n){put32(p,static_cast<uint32_t>(n>>32));put32(p+4,static_cast<uint32_t>(n));}
static bool nonempty(const char *p){return p && p[0] && std::strlen(p)<4096;}
static bool valid_identity(const char *p){return p && p[0] && std::strlen(p)<255;}
static bool valid_token(const char *p){
  if (!p) return false;
  size_t n=strnlen(p,65);
  if (n<16||n>64)return false;
  for(size_t i=0;i<n;++i)if(static_cast<unsigned char>(p[i])<33||static_cast<unsigned char>(p[i])>126)return false;
  return true;
}
static void set_timeout(int fd,uint32_t millis) {
  timeval tv{};tv.tv_sec=millis/1000;tv.tv_usec=(millis%1000)*1000;
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
}
static int plain_read(int fd,unsigned char *p,size_t len) {
  for(size_t n=0;n<len;){
    ssize_t got=recv(fd,p+n,len-n,0);
    if(got==0)return -ECONNRESET;
    if(got<0){if(errno==EINTR)continue;return -errno;}
    n+=static_cast<size_t>(got);
  }return 0;
}
static int plain_write(int fd,const unsigned char *p,size_t len) {
  for(size_t n=0;n<len;){
#ifdef MSG_NOSIGNAL
    ssize_t wrote=send(fd,p+n,len-n,MSG_NOSIGNAL);
#else
    ssize_t wrote=send(fd,p+n,len-n,0);
#endif
    if(wrote==0)return -EPIPE;
    if(wrote<0){if(errno==EINTR)continue;return -errno;}
    n+=static_cast<size_t>(wrote);
  }return 0;
}
static int ssl_read_full(SSL *s,unsigned char *p,size_t len) {
  for(size_t n=0;n<len;){
    int got=SSL_read(s,p+n,static_cast<int>(len-n));
    if(got>0){n+=static_cast<size_t>(got);continue;}
    int error=SSL_get_error(s,got);
    if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE)return -ETIMEDOUT;
    return error==SSL_ERROR_ZERO_RETURN?-ECONNRESET:-EIO;
  }return 0;
}
static int ssl_write_full(SSL *s,const unsigned char *p,size_t len) {
  for(size_t n=0;n<len;){
    int written=SSL_write(s,p+n,static_cast<int>(len-n));
    if(written>0){n+=static_cast<size_t>(written);continue;}
    int error=SSL_get_error(s,written);
    if(error==SSL_ERROR_WANT_READ||error==SSL_ERROR_WANT_WRITE)return -ETIMEDOUT;
    return error==SSL_ERROR_ZERO_RETURN?-ECONNRESET:-EIO;
  }return 0;
}
static SSL_CTX *make_context(bool server,const char *ca,const char *cert,const char *key) {
  SSL_CTX *ctx=SSL_CTX_new(server?TLS_server_method():TLS_client_method());
  if(!ctx)return nullptr;
  if(SSL_CTX_set_min_proto_version(ctx,TLS1_3_VERSION)!=1 ||
     SSL_CTX_set_max_proto_version(ctx,TLS1_3_VERSION)!=1 ||
     SSL_CTX_load_verify_locations(ctx,ca,nullptr)!=1 ||
     SSL_CTX_use_certificate_chain_file(ctx,cert)!=1 ||
     SSL_CTX_use_PrivateKey_file(ctx,key,SSL_FILETYPE_PEM)!=1 ||
     SSL_CTX_check_private_key(ctx)!=1){SSL_CTX_free(ctx);return nullptr;}
  SSL_CTX_set_verify(ctx,server?(SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT):SSL_VERIFY_PEER,nullptr);
  SSL_CTX_set_verify_depth(ctx,4);
  SSL_CTX_set_options(ctx,SSL_OP_NO_COMPRESSION);
  return ctx;
}
static int make_socket(const char *ipv4,uint16_t port,bool listen_socket,
                       int &fd,uint16_t &bound,uint32_t connect_ms=5000) {
  sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(port);
  if(inet_pton(AF_INET,ipv4,&addr.sin_addr)!=1)return -EINVAL;
  fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -errno;
  if(listen_socket) {
    if(bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))!=0 ||
       listen(fd,64)!=0){int e=errno;close(fd);fd=-1;return -e;}
    socklen_t length=sizeof(addr);
    if(getsockname(fd,reinterpret_cast<sockaddr*>(&addr),&length)!=0){int e=errno;close(fd);fd=-1;return -e;}
    bound=ntohs(addr.sin_port);return 0;
  }
  const int flags=fcntl(fd,F_GETFL,0);
  if(flags<0){int e=errno;close(fd);fd=-1;return -e;}
  if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)!=0){int e=errno;close(fd);fd=-1;return -e;}
  int result=connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
  if(result<0 && errno==EINPROGRESS){
    pollfd p{fd,POLLOUT,0};
    do{result=poll(&p,1,static_cast<int>(connect_ms));}while(result<0&&errno==EINTR);
    if(result==0){close(fd);fd=-1;return -ETIMEDOUT;}
    if(result<0){int e=errno;close(fd);fd=-1;return -e;}
    int error=0;socklen_t length=sizeof(error);
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)!=0){int e=errno;close(fd);fd=-1;return -e;}
    if(error){close(fd);fd=-1;return -error;}
  }else if(result<0){int e=errno;close(fd);fd=-1;return -e;}
  if(fcntl(fd,F_SETFL,flags)!=0){int e=errno;close(fd);fd=-1;return -e;}
  return 0;
}
static int backend_connect(uint16_t port,const std::string &token) {
  int fd=-1;uint16_t ignored=0;
  int rc=make_socket("127.0.0.1",port,false,fd,ignored);
  if(rc)return rc;
  set_timeout(fd,5000);
  unsigned char len[4];put32(len,static_cast<uint32_t>(token.size()));
  rc=plain_write(fd,len,4);
  if(!rc)rc=plain_write(fd,reinterpret_cast<const unsigned char*>(token.data()),token.size());
  unsigned char response[16];
  if(!rc)rc=plain_read(fd,response,16);
  if(!rc && get32(response)!=magic)rc=-EPROTO;
  if(!rc)rc=static_cast<int32_t>(get32(response+4));
  if(rc){close(fd);return rc;}
  return fd;
}
}

struct nq_pgas_tls_gateway {
  SSL_CTX *ctx=nullptr;
  std::string token,allowed_dns;
  int listener=-1;
  uint16_t port=0,backend_port=0;
  std::atomic<bool> stopping{false};
  std::thread acceptor;
  std::mutex clients_mu;
  std::vector<int> sockets;
  std::vector<std::thread> workers;
};
struct nq_pgas_tls_client {
  SSL_CTX *ctx=nullptr;
  SSL *ssl=nullptr;
  int fd=-1;
  std::mutex mu;
  uint32_t epoch = 0;
};
namespace {
static void worker(nq_pgas_tls_gateway *g,int fd){
  set_timeout(fd,5000);
  SSL *s=SSL_new(g->ctx);
  int backend=-1;
  if(s && SSL_set_fd(s,fd)==1 && SSL_accept(s)==1 &&
     SSL_get_verify_result(s)==X509_V_OK) {
    X509 *cert=SSL_get1_peer_certificate(s);
    bool allowed=cert && X509_check_host(cert,g->allowed_dns.c_str(),
                                          g->allowed_dns.size(),0,nullptr)==1;
    X509_free(cert);
    if(allowed)backend=backend_connect(g->backend_port,g->token);
  }
  if(backend>=0){
    unsigned char request[32],response[16];
    while(!g->stopping){
      if(ssl_read_full(s,request,sizeof(request))!=0)break;
      if(plain_write(backend,request,sizeof(request))!=0)break;
      if(plain_read(backend,response,sizeof(response))!=0)break;
      if(ssl_write_full(s,response,sizeof(response))!=0)break;
    }
    shutdown(backend,SHUT_RDWR);close(backend);
  }
  if(s)SSL_free(s);
  std::lock_guard<std::mutex> lock(g->clients_mu);
  auto it=std::find(g->sockets.begin(),g->sockets.end(),fd);
  if(it!=g->sockets.end())g->sockets.erase(it);
  shutdown(fd,SHUT_RDWR);close(fd);
}
static void accept_loop(nq_pgas_tls_gateway *g) {
  while(!g->stopping) {
    pollfd pfd{g->listener,POLLIN,0};
    int rc=poll(&pfd,1,200);
    if(rc<0){if(errno==EINTR)continue;break;}
    if(rc==0||!(pfd.revents&POLLIN))continue;
    int fd=accept(g->listener,nullptr,nullptr);
    if(fd<0){if(errno==EINTR)continue;continue;}
    std::lock_guard<std::mutex> lock(g->clients_mu);
    if(g->workers.size()>=max_sessions){shutdown(fd,SHUT_RDWR);close(fd);continue;}
    try{
      g->sockets.push_back(fd);
      g->workers.emplace_back([g,fd]{worker(g,fd);});
    }catch(...){
      g->sockets.erase(std::remove(g->sockets.begin(),g->sockets.end(),fd),g->sockets.end());
      shutdown(fd,SHUT_RDWR);close(fd);
    }
  }
}
static int tls_request_locked(nq_pgas_tls_client *c,uint32_t op,uint32_t index,
                              int64_t a,int64_t b,int64_t *out,
                              const unsigned char *payload=nullptr){
  if(!c)return -EINVAL;
  if(!c->ssl)return -ENOTCONN;
  unsigned char req[32]{},response[16];
  put32(req,magic);put32(req+4,op);put32(req+8,index);
  put32(req+12,c->epoch);
  if(payload)std::memcpy(req+16,payload,16);
  else {put64(req+16,static_cast<uint64_t>(a));put64(req+24,static_cast<uint64_t>(b));}
  int rc=ssl_write_full(c->ssl,req,sizeof(req));
  if(!rc)rc=ssl_read_full(c->ssl,response,sizeof(response));
  if(rc){SSL_free(c->ssl);c->ssl=nullptr;shutdown(c->fd,SHUT_RDWR);close(c->fd);c->fd=-1;return rc;}
  if(get32(response)!=magic)return -EPROTO;
  int status=static_cast<int32_t>(get32(response+4));
  if(status==0 && out)*out=static_cast<int64_t>(get64(response+8));
  return status;
}
static int tls_request(nq_pgas_tls_client *c,uint32_t op,uint32_t index,
                       int64_t a,int64_t b,int64_t *out){
  if(!c)return -EINVAL;
  std::lock_guard<std::mutex> lock(c->mu);
  return tls_request_locked(c,op,index,a,b,out);
}
}
extern "C" {
int nq_pgas_tls_gateway_start(const char *bind_ipv4,uint16_t port,
    uint16_t local_pgas_port,const char *backend_token,
    const char *ca_pem,const char *server_cert_pem,const char *server_key_pem,
    const char *allowed_client_dns,nq_pgas_tls_gateway **out){
  if(!out||!nonempty(bind_ipv4)||!local_pgas_port||!valid_token(backend_token)||
     !nonempty(ca_pem)||!nonempty(server_cert_pem)||!nonempty(server_key_pem)||
     !valid_identity(allowed_client_dns))return -EINVAL;
  *out=nullptr;
  std::unique_ptr<nq_pgas_tls_gateway> g(new(std::nothrow)nq_pgas_tls_gateway);
  if(!g)return -ENOMEM;
  g->ctx=make_context(true,ca_pem,server_cert_pem,server_key_pem);
  if(!g->ctx)return -EACCES;
  g->token=backend_token;g->backend_port=local_pgas_port;g->allowed_dns=allowed_client_dns;
  int rc=make_socket(bind_ipv4,port,true,g->listener,g->port);
  if(rc){SSL_CTX_free(g->ctx);return rc;}
  try{g->acceptor=std::thread([p=g.get()]{accept_loop(p);});}
  catch(...){close(g->listener);SSL_CTX_free(g->ctx);return -EAGAIN;}
  *out=g.release();return 0;
}
uint16_t nq_pgas_tls_gateway_port(nq_pgas_tls_gateway *g){return g?g->port:0;}
int nq_pgas_tls_gateway_stop(nq_pgas_tls_gateway *g){
  if(!g)return -EINVAL;
  g->stopping=true;
  shutdown(g->listener,SHUT_RDWR);close(g->listener);
  if(g->acceptor.joinable())g->acceptor.join();
  {std::lock_guard<std::mutex> lock(g->clients_mu);
   for(int fd:g->sockets)shutdown(fd,SHUT_RDWR);}
  for(auto &worker:g->workers)if(worker.joinable())worker.join();
  SSL_CTX_free(g->ctx);delete g;return 0;
}
int nq_pgas_tls_client_connect(const char *server_ipv4,uint16_t port,
    const char *ca_pem,const char *client_cert_pem,const char *client_key_pem,
    const char *expected_server_dns,uint32_t timeout_ms,nq_pgas_tls_client **out){
  if(!out||!nonempty(server_ipv4)||!port||!nonempty(ca_pem)||
     !nonempty(client_cert_pem)||!nonempty(client_key_pem)||
     !valid_identity(expected_server_dns)||timeout_ms==0||timeout_ms>60000)return -EINVAL;
  *out=nullptr;
  std::unique_ptr<nq_pgas_tls_client> c(new(std::nothrow)nq_pgas_tls_client);
  if(!c)return -ENOMEM;
  c->ctx=make_context(false,ca_pem,client_cert_pem,client_key_pem);
  if(!c->ctx)return -EACCES;
  uint16_t ignored=0;
  int rc=make_socket(server_ipv4,port,false,c->fd,ignored,timeout_ms);
  if(rc){SSL_CTX_free(c->ctx);return rc;}
  set_timeout(c->fd,timeout_ms);
  c->ssl=SSL_new(c->ctx);
  if(!c->ssl||SSL_set_fd(c->ssl,c->fd)!=1||
     SSL_set_tlsext_host_name(c->ssl,expected_server_dns)!=1||
     SSL_set1_host(c->ssl,expected_server_dns)!=1||
     SSL_connect(c->ssl)!=1||SSL_get_verify_result(c->ssl)!=X509_V_OK){
    if(c->ssl) SSL_free(c->ssl);
    close(c->fd);
    SSL_CTX_free(c->ctx);
    return -EACCES;
  }
  *out=c.release();return 0;
}
int nq_pgas_tls_client_set_epoch(nq_pgas_tls_client *c,uint32_t epoch){
  if(!c)return -EINVAL;
  std::lock_guard<std::mutex> lock(c->mu);
  if(!c->ssl)return -ENOTCONN;
  c->epoch=epoch;
  return 0;
}
int nq_pgas_tls_client_close(nq_pgas_tls_client *c){
  if(!c)return -EINVAL;
  {std::lock_guard<std::mutex> lock(c->mu);
   if(c->ssl)SSL_free(c->ssl);
   if(c->fd>=0){shutdown(c->fd,SHUT_RDWR);close(c->fd);}}
  SSL_CTX_free(c->ctx);delete c;return 0;
}
int nq_pgas_tls_ping(nq_pgas_tls_client *c){return tls_request(c,1,0,0,0,nullptr);}
int nq_pgas_tls_read(nq_pgas_tls_client *c,uint32_t index,int64_t *out){return out?tls_request(c,2,index,0,0,out):-EINVAL;}
int nq_pgas_tls_write(nq_pgas_tls_client *c,uint32_t index,int64_t value){return tls_request(c,3,index,value,0,nullptr);}
int nq_pgas_tls_compare_exchange(nq_pgas_tls_client *c,uint32_t index,int64_t expected,int64_t desired,int64_t *observed){
  return observed?tls_request(c,4,index,expected,desired,observed):-EINVAL;
}
int nq_pgas_tls_fetch_add(nq_pgas_tls_client *c,uint32_t index,int64_t delta,int64_t *previous){
  return previous?tls_request(c,5,index,delta,0,previous):-EINVAL;
}
int nq_pgas_tls_execute_program(nq_pgas_tls_client *c,const uint8_t *code,
    size_t length,int64_t input,uint32_t fuel,int64_t *out){
  if(!c||!code||!out||!length||length>256||!fuel||fuel>4096)return -EINVAL;
  std::lock_guard<std::mutex> lock(c->mu);
  for(size_t at=0;at<length;at+=16){
    unsigned char chunk[16]{};
    std::memcpy(chunk,code+at,std::min<size_t>(16,length-at));
    int rc=tls_request_locked(c,8,static_cast<uint32_t>(at),0,0,nullptr,chunk);
    if(rc)return rc;
  }
  return tls_request_locked(c,9,static_cast<uint32_t>(length),input,fuel,out);
}
int nq_pgas_tls_barrier(nq_pgas_tls_client *c){return tls_request(c,6,0,0,0,nullptr);}
int nq_pgas_tls_run_task(nq_pgas_tls_client *c,uint32_t id,int64_t input,int64_t *out){
  if(!out||id==0||id>65535)return -EINVAL;
  return tls_request(c,7,id,input,0,out);
}
}
