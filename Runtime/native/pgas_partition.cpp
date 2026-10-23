#include "noqeri/pgas_partition.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct Binding {
  uint64_t elements=0;
  uint32_t nodes=0, strategy=0;
  std::vector<nq_pgas_client *> clients;
};
std::mutex bindings_mu;
std::unordered_map<std::string,Binding> bindings;
bool good_name(const char *symbol) {
  if(!symbol)return false;
  const size_t len=strnlen(symbol,65);
  if(len==0||len>64)return false;
  for(size_t i=0;i<len;++i) {
    const char ch=symbol[i];
    if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
         (ch>='0'&&ch<='9')||ch=='_'))return false;
  }
  return true;
}
[[noreturn]] void fail_stop(const char *reason,int error) {
  std::fprintf(stderr,"NQR-PGAS401: native distributed array access failed: %s (status=%d)\n",
               reason,error);
  std::fflush(stderr);
  std::abort();
}
int64_t access(const char* name,int64_t index,uint64_t elements,
               uint32_t nodes,uint32_t strategy,bool write,int64_t value) {
  if(!good_name(name)||index<0||elements==0||nodes==0||nodes>1024||strategy>1)
    fail_stop("invalid declaration or index",-EINVAL);
  std::lock_guard<std::mutex> lock(bindings_mu);
  auto it=bindings.find(name);
  if(it==bindings.end())fail_stop("partition nodes not bound",-ENOTCONN);
  const Binding &b=it->second;
  if(b.elements!=elements||b.nodes!=nodes||b.strategy!=strategy)
    fail_stop("runtime topology does not match compiled metadata",-ESTALE);
  uint32_t owner=0,local=0;
  const int rc=nq_pgas_location(static_cast<uint64_t>(index),elements,
                                 nodes,strategy,&owner,&local);
  if(rc!=0)fail_stop("global index out of bounds",rc);
  if(owner>=b.clients.size()||!b.clients[owner])
    fail_stop("node owner is unavailable",-ENOTCONN);
  int64_t result=0;
  const int status=write ? nq_pgas_write(b.clients[owner],local,value) :
                           nq_pgas_read(b.clients[owner],local,&result);
  if(status!=0)fail_stop("remote request failed or epoch is stale",status);
  return write?value:result;
}
}
extern "C" {
int nq_pgas_partition_bind(const char *symbol,uint64_t elements,
    uint32_t nodes,uint32_t strategy,nq_pgas_client *const *clients) {
  if(!good_name(symbol)||!elements||!nodes||nodes>1024||strategy>1||!clients)
    return -EINVAL;
  for(uint32_t i=0;i<nodes;++i)if(!clients[i])return -EINVAL;
  // Native array names are unique for the entry scope; duplicate binding is
  // not silently replaced while another thread may be reading the array.
  std::lock_guard<std::mutex> lock(bindings_mu);
  if(bindings.find(symbol)!=bindings.end())return -EEXIST;
  try {
    Binding b;
    b.elements=elements;b.nodes=nodes;b.strategy=strategy;
    b.clients.assign(clients,clients+nodes);
    bindings.emplace(symbol,std::move(b));
  }catch(const std::bad_alloc&){return -ENOMEM;}
  return 0;
}
int nq_pgas_partition_unbind(const char *symbol) {
  if(!good_name(symbol))return -EINVAL;
  std::lock_guard<std::mutex> lock(bindings_mu);
  return bindings.erase(symbol)==1?0:-ENOENT;
}
int64_t nq_pgas_compiler_partition_read(const char *symbol,int64_t index,
    uint64_t elements,uint32_t nodes,uint32_t strategy) {
  return access(symbol,index,elements,nodes,strategy,false,0);
}
int64_t nq_pgas_compiler_partition_write(const char *symbol,int64_t index,
    uint64_t elements,uint32_t nodes,uint32_t strategy,int64_t value) {
  return access(symbol,index,elements,nodes,strategy,true,value);
}
}
