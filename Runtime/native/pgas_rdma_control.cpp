#include "noqeri/pgas_rdma_control.h"
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>

namespace {
constexpr size_t wire_size=52;
constexpr uint32_t magic=0x4e515244; // NQRD
constexpr uint32_t protocol=1;
static void put32(unsigned char*p,uint32_t x){x=htonl(x);std::memcpy(p,&x,4);}
static uint32_t get32(const unsigned char*p){uint32_t x;std::memcpy(&x,p,4);return ntohl(x);}
static void put16(unsigned char*p,uint16_t x){x=htons(x);std::memcpy(p,&x,2);}
static uint16_t get16(const unsigned char*p){uint16_t x;std::memcpy(&x,p,2);return ntohs(x);}
static void put64(unsigned char*p,uint64_t x){put32(p,static_cast<uint32_t>(x>>32));put32(p+4,static_cast<uint32_t>(x));}
static uint64_t get64(const unsigned char*p){return (uint64_t(get32(p))<<32)|get32(p+4);}
static bool nonempty(const char*p){return p&&*p&&std::strlen(p)<4096;}
static bool identity(const char*p){return p&&*p&&std::strlen(p)<255;}
static bool endpoint_valid(const nq_pgas_rdma_endpoint& e){
  if(!e.qp_number||e.psn>0xffffff||!e.rkey||!e.slots||e.slots>1000000||!e.address)return false;
  if(e.lid)return true;
  for(uint8_t v:e.gid)if(v)return true;
  return false;
}
static void encode(unsigned char*out,const nq_pgas_rdma_endpoint& e){
  std::memset(out,0,wire_size);
  put32(out,magic);put32(out+4,protocol);
  put32(out+8,e.qp_number);put32(out+12,e.psn);
  put32(out+16,e.rkey);put32(out+20,e.slots);
  put64(out+24,e.address);put16(out+32,e.lid);
  std::memcpy(out+36,e.gid,16);
}
static bool decode(const unsigned char*buf,nq_pgas_rdma_endpoint&out){
  if(get32(buf)!=magic||get32(buf+4)!=protocol||get16(buf+34)!=0)return false;
  nq_pgas_rdma_endpoint next{};
  next.qp_number=get32(buf+8);next.psn=get32(buf+12);
  next.rkey=get32(buf+16);next.slots=get32(buf+20);
  next.address=get64(buf+24);next.lid=get16(buf+32);
  std::memcpy(next.gid,buf+36,16);
  if(!endpoint_valid(next))return false;
  out=next;return true;
}
static SSL_CTX* context(bool server,const char*ca,const char*crt,const char*key){
  SSL_CTX*ctx=SSL_CTX_new(server?TLS_server_method():TLS_client_method());
  if(!ctx)return nullptr;
  if(SSL_CTX_set_min_proto_version(ctx,TLS1_3_VERSION)!=1 ||
     SSL_CTX_set_max_proto_version(ctx,TLS1_3_VERSION)!=1 ||
     SSL_CTX_load_verify_locations(ctx,ca,nullptr)!=1 ||
     SSL_CTX_use_certificate_chain_file(ctx,crt)!=1 ||
     SSL_CTX_use_PrivateKey_file(ctx,key,SSL_FILETYPE_PEM)!=1 ||
     SSL_CTX_check_private_key(ctx)!=1){SSL_CTX_free(ctx);return nullptr;}
  SSL_CTX_set_verify(ctx,server?(SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT):SSL_VERIFY_PEER,nullptr);
  SSL_CTX_set_verify_depth(ctx,4);
  SSL_CTX_set_options(ctx,SSL_OP_NO_COMPRESSION);
  return ctx;
}
static void timeout(int fd,uint32_t ms){
  timeval t{};t.tv_sec=ms/1000;t.tv_usec=(ms%1000)*1000;
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));
}
static int read_full(SSL*s,unsigned char*p,size_t n){
  for(size_t at=0;at<n;){
    int r=SSL_read(s,p+at,static_cast<int>(n-at));
    if(r<=0)return -EIO;
    at+=static_cast<size_t>(r);
  }
  return 0;
}
static int write_full(SSL*s,const unsigned char*p,size_t n){
  for(size_t at=0;at<n;){
    int r=SSL_write(s,p+at,static_cast<int>(n-at));
    if(r<=0)return -EIO;
    at+=static_cast<size_t>(r);
  }
  return 0;
}
static int listen_ipv4(const char*ipv4,uint16_t port,int&fd,uint16_t&actual){
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);
  if(inet_pton(AF_INET,ipv4,&address.sin_addr)!=1)return -EINVAL;
  fd=socket(AF_INET,SOCK_STREAM,0);
  if(fd<0)return -errno;
  if(bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0 ||
     listen(fd,4)!=0){int e=errno;close(fd);fd=-1;return -e;}
  socklen_t size=sizeof(address);
  if(getsockname(fd,reinterpret_cast<sockaddr*>(&address),&size)!=0){
    int e=errno;close(fd);fd=-1;return -e;
  }
  actual=ntohs(address.sin_port);return 0;
}
static int dial_ipv4(const char*ipv4,uint16_t port,uint32_t ms){
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);
  if(inet_pton(AF_INET,ipv4,&address.sin_addr)!=1)return -EINVAL;
  int fd=socket(AF_INET,SOCK_STREAM,0);
  if(fd<0)return -errno;
  int flags=fcntl(fd,F_GETFL,0);
  if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)!=0){
    int e=errno;close(fd);return -e;
  }
  int rc=connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address));
  if(rc<0 && errno==EINPROGRESS) {
    pollfd p{fd,POLLOUT,0};
    do{rc=poll(&p,1,static_cast<int>(ms));}while(rc<0&&errno==EINTR);
    if(rc==0){close(fd);return -ETIMEDOUT;}
    if(rc<0){int e=errno;close(fd);return -e;}
    int error=0;socklen_t size=sizeof(error);
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)!=0){int e=errno;close(fd);return -e;}
    if(error){close(fd);return -error;}
  }else if(rc<0){int e=errno;close(fd);return -e;}
  if(fcntl(fd,F_SETFL,flags)!=0){int e=errno;close(fd);return -e;}
  timeout(fd,ms);
  return fd;
}
}
struct nq_pgas_rdma_control_server{
  SSL_CTX*ctx=nullptr;
  int listener=-1;
  uint16_t port=0;
  std::string allowed_dns;
  std::mutex mu;
};
extern "C" {
int nq_pgas_rdma_control_server_start(const char*ip,uint16_t port,
 const char*ca,const char*crt,const char*key,const char*allowed,
 nq_pgas_rdma_control_server**out){
  if(!out||!nonempty(ip)||!nonempty(ca)||!nonempty(crt)||
     !nonempty(key)||!identity(allowed))return -EINVAL;
  *out=nullptr;
  auto*s=new(std::nothrow)nq_pgas_rdma_control_server;
  if(!s)return -ENOMEM;
  s->ctx=context(true,ca,crt,key);
  if(!s->ctx){delete s;return -EACCES;}
  s->allowed_dns=allowed;
  int rc=listen_ipv4(ip,port,s->listener,s->port);
  if(rc){SSL_CTX_free(s->ctx);delete s;return rc;}
  *out=s;return 0;
}
uint16_t nq_pgas_rdma_control_server_port(nq_pgas_rdma_control_server*s){
  return s?s->port:0;
}
int nq_pgas_rdma_control_server_exchange(nq_pgas_rdma_control_server*s,
 const nq_pgas_rdma_endpoint*local,nq_pgas_rdma_endpoint*remote,uint32_t ms){
  if(!s||!local||!remote||!endpoint_valid(*local)||!ms||ms>60000)return -EINVAL;
  *remote={};
  std::lock_guard<std::mutex> lock(s->mu);
  pollfd p{s->listener,POLLIN,0};
  int ready=0;
  do{ready=poll(&p,1,static_cast<int>(ms));}while(ready<0&&errno==EINTR);
  if(ready==0)return -ETIMEDOUT;
  if(ready<0)return -errno;
  int fd=accept(s->listener,nullptr,nullptr);
  if(fd<0)return -errno;
  timeout(fd,ms);
  SSL*ssl=SSL_new(s->ctx);
  int status=-EACCES;
  if(ssl && SSL_set_fd(ssl,fd)==1 && SSL_accept(ssl)==1 &&
     SSL_get_verify_result(ssl)==X509_V_OK){
    X509*cert=SSL_get1_peer_certificate(ssl);
    const bool allowed=cert&&X509_check_host(cert,s->allowed_dns.c_str(),
      s->allowed_dns.size(),0,nullptr)==1;
    X509_free(cert);
    if(allowed){
      unsigned char their[wire_size],ours[wire_size];
      encode(ours,*local);
      status=read_full(ssl,their,sizeof(their));
      if(!status){
        nq_pgas_rdma_endpoint peer{};
        if(!decode(their,peer))status=-EBADMSG;
        else{
          status=write_full(ssl,ours,sizeof(ours));
          if(!status)*remote=peer;
        }
      }
    }
  }
  if(ssl)SSL_free(ssl);
  shutdown(fd,SHUT_RDWR);close(fd);
  return status;
}
int nq_pgas_rdma_control_server_stop(nq_pgas_rdma_control_server*s){
  if(!s)return -EINVAL;
  {
    std::lock_guard<std::mutex> lock(s->mu);
    shutdown(s->listener,SHUT_RDWR);close(s->listener);
    SSL_CTX_free(s->ctx);
  }
  delete s;return 0;
}
int nq_pgas_rdma_control_client_exchange(const char*ip,uint16_t port,
 const char*ca,const char*crt,const char*key,const char*expected,
 const nq_pgas_rdma_endpoint*local,nq_pgas_rdma_endpoint*remote,uint32_t ms){
  if(!nonempty(ip)||!port||!nonempty(ca)||!nonempty(crt)||!nonempty(key)||
     !identity(expected)||!local||!remote||!endpoint_valid(*local)||
     !ms||ms>60000)return -EINVAL;
  *remote={};
  SSL_CTX*ctx=context(false,ca,crt,key);
  if(!ctx)return -EACCES;
  int fd=dial_ipv4(ip,port,ms);
  if(fd<0){SSL_CTX_free(ctx);return fd;}
  SSL*ssl=SSL_new(ctx);
  int status=-EACCES;
  if(ssl && SSL_set_fd(ssl,fd)==1 &&
     SSL_set_tlsext_host_name(ssl,expected)==1 &&
     SSL_set1_host(ssl,expected)==1 &&
     SSL_connect(ssl)==1 && SSL_get_verify_result(ssl)==X509_V_OK){
    unsigned char ours[wire_size],theirs[wire_size];
    encode(ours,*local);
    status=write_full(ssl,ours,sizeof(ours));
    if(!status){
      status=read_full(ssl,theirs,sizeof(theirs));
      if(!status){
        nq_pgas_rdma_endpoint peer{};
        if(!decode(theirs,peer))status=-EBADMSG;
        else *remote=peer;
      }
    }
  }
  if(ssl)SSL_free(ssl);
  shutdown(fd,SHUT_RDWR);close(fd);
  SSL_CTX_free(ctx);
  return status;
}
}
