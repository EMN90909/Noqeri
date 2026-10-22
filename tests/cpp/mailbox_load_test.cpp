#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/mailbox_extended.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <sys/socket.h>
#include <sys/resource.h>
#include <unistd.h>
int main(int argc,char**argv) {
  const int count=argc>1?std::atoi(argv[1]):2000;
  if(count<1||count>20000)return 2;
  rlimit limit{};assert(getrlimit(RLIMIT_NOFILE,&limit)==0);
  if(limit.rlim_cur<static_cast<rlim_t>(2*count+64)){
    std::fprintf(stderr,"SKIP: file descriptor limit insufficient for %d clients\n",count);
    return 77;
  }
  auto* reactor=nq_mail_reactor_create();assert(reactor);
  std::vector<int> readfds,writefds;
  readfds.reserve(count);writefds.reserve(count);
  for(int i=0;i<count;++i){
    int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);
    assert(nq_mail_set_nonblocking(fd[0])==0);
    assert(nq_mail_set_nonblocking(fd[1])==0);
    assert(nq_mail_reactor_add(reactor,fd[0],1)==0);
    readfds.push_back(fd[0]);writefds.push_back(fd[1]);
  }
  const auto start=std::chrono::steady_clock::now();
  for(int fd:writefds)assert(write(fd,"z",1)==1);
  std::vector<nq_mail_event> events(std::min(count,1024));
  int completed=0;
  while(completed<count){
    int n=nq_mail_reactor_wait(reactor,events.data(),static_cast<int>(events.size()),2000);
    assert(n>0);
    for(int i=0;i<n;++i){
      char c=0;
      int read_result=read(events[i].fd,&c,1);
      if(read_result==1){assert(c=='z');++completed;}
      assert(nq_mail_reactor_remove(reactor,events[i].fd)==0);
    }
  }
  const auto millis=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
  std::printf("PASS: %d concurrent ready descriptors in %lld ms\n",count,static_cast<long long>(millis));
  for(int fd:readfds)close(fd);
  for(int fd:writefds)close(fd);
  nq_mail_reactor_destroy(reactor);
}
