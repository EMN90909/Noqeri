#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "noqeri/mailbox_extended.h"
static void reactor_test(){
  auto*r=nq_mail_reactor_create();assert(r);
  constexpr int count=512;
  std::vector<int> readers,writers;
  for(int i=0;i<count;++i){
    int f[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,f)==0);
    readers.push_back(f[0]);writers.push_back(f[1]);
    assert(nq_mail_set_nonblocking(f[0])==0);
    assert(nq_mail_reactor_add(r,f[0],1)==0);
  }
  for(int fd:writers)assert(write(fd,"X",1)==1);
  std::vector<nq_mail_event> ev(count);
  bool seen[count]{};int remaining=count;
  for(int tries=0;tries<10&&remaining;++tries){
    int n=nq_mail_reactor_wait(r,ev.data(),count,1000);assert(n>0);
    for(int j=0;j<n;++j){
      if(!(ev[j].events&1))continue;
      for(int i=0;i<count;++i)if(readers[i]==ev[j].fd&&!seen[i]){
        char c;assert(read(ev[j].fd,&c,1)==1&&c=='X');
        seen[i]=true;--remaining;break;
      }
    }
  }
  assert(remaining==0);
  for(int fd:readers){assert(nq_mail_reactor_remove(r,fd)==0);close(fd);}
  for(int fd:writers)close(fd);
  nq_mail_reactor_destroy(r);
}
static void persistence_test(){
  char dir[]="/tmp/noqeri-durable-XXXXXX";assert(mkdtemp(dir));
  const char msg[]="hello durable mailbox";
  assert(nq_mail_commit_message(dir,"abc_123",msg,sizeof(msg))==0);
  assert(nq_mail_commit_message(dir,"abc_123",msg,sizeof(msg))==-EEXIST);
  assert(nq_mail_commit_message(dir,"../escape",msg,sizeof(msg))==-EINVAL);
  assert(nq_mail_commit_message(dir,"space name",msg,sizeof(msg))==-EINVAL);
  std::ifstream in(std::string(dir)+"/abc_123.msg",std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)),{});
  assert(bytes.size()==sizeof(msg));
  assert(std::memcmp(bytes.data(),msg,sizeof(msg))==0);
  in.close();
  // Publish in parallel: each committed ID must be present with exact data.
  constexpr int count=128;
  std::atomic<int> ok{0};
  std::vector<std::thread> threads;
  for(int t=0;t<8;++t)threads.emplace_back([&,t]{
    for(int j=t;j<count;j+=8){
      std::string id="msg_"+std::to_string(j);
      if(nq_mail_commit_message(dir,id.c_str(),msg,sizeof(msg))==0)++ok;
    }
  });
  for(auto&t:threads)t.join();assert(ok==count);
  for(int j=0;j<count;++j){
    std::string p=std::string(dir)+"/msg_"+std::to_string(j)+".msg";
    std::ifstream f(p,std::ios::binary);
    std::string data((std::istreambuf_iterator<char>(f)),{});
    assert(data.size()==sizeof(msg)&&std::memcmp(data.data(),msg,sizeof(msg))==0);
    assert(unlink(p.c_str())==0);
  }
  assert(unlink((std::string(dir)+"/abc_123.msg").c_str())==0);
  assert(rmdir(dir)==0);
}
int main(){reactor_test();persistence_test();}
