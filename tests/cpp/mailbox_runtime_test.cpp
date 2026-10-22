#include "noqeri/mailbox_runtime.h"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
static void job(void*ctx){static_cast<std::atomic<int>*>(ctx)->fetch_add(1);}
int main(){
  nq_mail_pool*p=nq_mail_pool_create(16);assert(p);
  void*a=nq_mail_pool_alloc(p,8);assert(a);assert(nq_mail_pool_used(p)==8);
  assert(!nq_mail_pool_alloc(p,9));assert(nq_mail_pool_destroy(p)==-EBUSY);
  assert(nq_mail_pool_free(p,a)==0);assert(nq_mail_pool_used(p)==0);
  assert(nq_mail_pool_destroy(p)==0);
  std::atomic<int> completed{0};
  nq_mail_workers*w=nq_mail_workers_create(2,100);assert(w);
  for(int i=0;i<50;++i)assert(nq_mail_workers_submit(w,job,&completed)==0);
  assert(nq_mail_workers_shutdown(w)==0);assert(completed==50);
  int listener=nq_mail_listen_loopback(0,8);assert(listener>=0);
  int port=nq_mail_local_port(listener);assert(port>0);
  int client=nq_mail_connect_loopback((uint16_t)port,2000);assert(client>=0);
  int server=nq_mail_accept(listener,2000);assert(server>=0);
  const char txt[]="ping";assert(nq_mail_write(client,txt,4)==4);
  char buf[8]{};assert(nq_mail_read(server,buf,4)==4);
  assert(std::memcmp(buf,txt,4)==0);
  assert(nq_mail_close(server)==0);assert(nq_mail_close(client)==0);assert(nq_mail_close(listener)==0);
  char dir[]="/tmp/noqeri-mailbox-XXXXXX";assert(mkdtemp(dir));
  assert(nq_mail_atomic_write(dir,"../bad",txt,4)==-EINVAL);
  assert(nq_mail_atomic_write(dir,"message",txt,4)==0);
  std::ifstream in(std::string(dir)+"/message",std::ios::binary);
  std::string contents((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
  assert(contents=="ping");
  assert(unlink((std::string(dir)+"/message").c_str())==0);assert(rmdir(dir)==0);
}
