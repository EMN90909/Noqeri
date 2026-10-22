#include "noqeri/mailbox_runtime.h"
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
struct nq_mail_pool {
  std::mutex mu;
  size_t max_bytes, used=0;
  std::unordered_map<void*,size_t> blocks;
  explicit nq_mail_pool(size_t n):max_bytes(n){}
};
struct nq_mail_workers {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<std::pair<nq_mail_job_fn,void*>> jobs;
  std::vector<std::thread> threads;
  size_t limit;
  bool stopping=false;
  explicit nq_mail_workers(size_t l):limit(l){}
};
extern "C" {
int nq_mail_listen_loopback(uint16_t port,int backlog) {
  if(backlog<=0)return -EINVAL;
  int s=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);
  if(s<0)return -errno;
  sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(port);
  if(bind(s,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))||listen(s,backlog)){int e=errno;close(s);return -e;}
  return s;
}
int nq_mail_accept(int listener,int timeout_ms) {
  if(timeout_ms<0)return -EINVAL;
  pollfd p{listener,POLLIN,0};
  int r;do{r=poll(&p,1,timeout_ms);}while(r<0&&errno==EINTR);
  if(r<0)return -errno;
  if(r==0)return -ETIMEDOUT;
  if(!(p.revents&POLLIN))return -EIO;
  int fd=accept(listener,nullptr,nullptr);
  if(fd<0)return -errno;
  int flags=fcntl(fd,F_GETFD);if(flags>=0)fcntl(fd,F_SETFD,flags|FD_CLOEXEC);
  return fd;
}
int nq_mail_connect_loopback(uint16_t port,int timeout_ms) {
  if(timeout_ms<0)return -EINVAL;
  int fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)return -errno;
  int flags=fcntl(fd,F_GETFL);if(flags<0){int e=errno;close(fd);return -e;}
  if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){int e=errno;close(fd);return -e;}
  sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);addr.sin_port=htons(port);
  int r=connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
  if(r<0&&errno!=EINPROGRESS){int e=errno;close(fd);return -e;}
  if(r<0){
    pollfd p{fd,POLLOUT,0};do{r=poll(&p,1,timeout_ms);}while(r<0&&errno==EINTR);
    if(r<=0){int e=r==0?ETIMEDOUT:errno;close(fd);return -e;}
    int e=0;socklen_t n=sizeof(e);
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&e,&n)<0){e=errno;close(fd);return -e;}
    if(e){close(fd);return -e;}
  }
  if(fcntl(fd,F_SETFL,flags)<0){int e=errno;close(fd);return -e;}
  return fd;
}
int64_t nq_mail_read(int fd,void* buf,size_t n) {
  if(!buf&&n)return -EINVAL;
  ssize_t r;do{r=read(fd,buf,n);}while(r<0&&errno==EINTR);
  return r<0?-errno:r;
}
int64_t nq_mail_write(int fd,const void* buf,size_t n) {
  if(!buf&&n)return -EINVAL;
  ssize_t r;do{r=write(fd,buf,n);}while(r<0&&errno==EINTR);
  return r<0?-errno:r;
}
int nq_mail_close(int fd){return close(fd)==0?0:-errno;}
int nq_mail_local_port(int fd) {
  sockaddr_in addr{};socklen_t n=sizeof(addr);
  if(getsockname(fd,reinterpret_cast<sockaddr*>(&addr),&n)<0)return -errno;
  if(addr.sin_family!=AF_INET)return -EAFNOSUPPORT;
  return ntohs(addr.sin_port);
}
static bool valid_base(const char* s) {
  if(!s||!*s||std::strcmp(s,".")==0||std::strcmp(s,"..")==0)return false;
  for(const char*p=s;*p;++p)if(*p=='/'||*p=='\\')return false;
  return true;
}
int nq_mail_atomic_write(const char* dir,const char* base,const void* data,size_t size) {
  if(!dir||!valid_base(base)||(!data&&size))return -EINVAL;
  int d=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
  if(d<0)return -errno;
  static std::atomic<uint64_t> sequence{0};
  std::string temp=std::string(".nqmail-")+std::to_string(getpid())+"-"+std::to_string(sequence.fetch_add(1));
  int fd=openat(d,temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
  if(fd<0){int e=errno;close(d);return -e;}
  int error=0;size_t done=0;
  while(done<size) {
    ssize_t n=write(fd,static_cast<const char*>(data)+done,size-done);
    if(n<0&&errno==EINTR)continue;
    if(n<=0){error=n<0?errno:EIO;break;}
    done+=static_cast<size_t>(n);
  }
  if(!error&&fsync(fd)<0)error=errno;
  if(close(fd)<0&&!error)error=errno;
  if(!error&&renameat(d,temp.c_str(),d,base)<0)error=errno;
  if(!error&&fsync(d)<0)error=errno;
  if(error)unlinkat(d,temp.c_str(),0);
  close(d);
  return error?-error:0;
}
nq_mail_pool* nq_mail_pool_create(size_t max) {
  try{return new nq_mail_pool(max);}catch(...){return nullptr;}
}
void* nq_mail_pool_alloc(nq_mail_pool*p,size_t n) {
  if(!p||!n)return nullptr;
  std::lock_guard<std::mutex> lock(p->mu);
  if(n>p->max_bytes-p->used)return nullptr;
  void* mem=std::malloc(n);if(!mem)return nullptr;
  try{p->blocks.emplace(mem,n);}catch(...){std::free(mem);return nullptr;}
  p->used+=n;return mem;
}
int nq_mail_pool_free(nq_mail_pool*p,void*mem) {
  if(!p||!mem)return -EINVAL;
  std::lock_guard<std::mutex> lock(p->mu);
  auto it=p->blocks.find(mem);if(it==p->blocks.end())return -EINVAL;
  p->used-=it->second;p->blocks.erase(it);std::free(mem);return 0;
}
size_t nq_mail_pool_used(nq_mail_pool*p) {
  if(!p)return 0;std::lock_guard<std::mutex>lock(p->mu);return p->used;
}
int nq_mail_pool_destroy(nq_mail_pool*p) {
  if(!p)return -EINVAL;
  {std::lock_guard<std::mutex> lock(p->mu);if(!p->blocks.empty())return -EBUSY;}
  delete p;return 0;
}
nq_mail_workers* nq_mail_workers_create(size_t count,size_t limit) {
  if(!count||!limit||count>256)return nullptr;
  nq_mail_workers*p=nullptr;
  try{
    p=new nq_mail_workers(limit);
    for(size_t i=0;i<count;++i)p->threads.emplace_back([p] {
      for(;;) {
        std::pair<nq_mail_job_fn,void*> job;
        {std::unique_lock<std::mutex> lock(p->mu);
          p->cv.wait(lock,[p]{return p->stopping||!p->jobs.empty();});
          if(p->jobs.empty()&&p->stopping)return;
          job=p->jobs.front();p->jobs.pop_front();
        }
        try{job.first(job.second);}catch(...){/* C callbacks must not throw across ABI. */}
      }
    });
    return p;
  }catch(...) {
    if(p){ {std::lock_guard<std::mutex> lock(p->mu);p->stopping=true;}
      p->cv.notify_all();for(auto&t:p->threads)if(t.joinable())t.join();delete p;}
    return nullptr;
  }
}
int nq_mail_workers_submit(nq_mail_workers*p,nq_mail_job_fn f,void*ctx) {
  if(!p||!f)return -EINVAL;
  {std::lock_guard<std::mutex> lock(p->mu);
    if(p->stopping)return -ESHUTDOWN;
    if(p->jobs.size()>=p->limit)return -EAGAIN;
    try{p->jobs.emplace_back(f,ctx);}catch(...){return -ENOMEM;}
  }
  p->cv.notify_one();return 0;
}
int nq_mail_workers_shutdown(nq_mail_workers*p) {
  if(!p)return -EINVAL;
  {std::lock_guard<std::mutex>lock(p->mu);p->stopping=true;}
  p->cv.notify_all();
  for(auto&t:p->threads)if(t.joinable())t.join();
  delete p;return 0;
}
}
