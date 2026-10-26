// SPDX-License-Identifier: Apache-2.0
#include "owned_collections.h"
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
namespace {
constexpr std::size_t maxElements=1'000'000;
struct Collection {
    int64_t kind;
    std::vector<std::int64_t> list;
    std::unordered_map<std::int64_t,std::int64_t> map;
    std::unordered_set<std::int64_t> set;
};
std::mutex mu;
std::map<std::int64_t,Collection> objects;
std::int64_t nextId=1;
Collection* lookup(std::int64_t id) {
    const auto found=objects.find(id);
    return found==objects.end()?nullptr:&found->second;
}
std::size_t sizeOf(const Collection& c) {
    switch(c.kind){case 1:return c.list.size();case 2:return c.map.size();default:return c.set.size();}
}
}
extern "C" int64_t nq_collection_new(int64_t kind) {
    if(kind<1||kind>3)return -1;
    std::lock_guard<std::mutex> guard(mu);
    if(nextId==std::numeric_limits<std::int64_t>::max())return -2;
    try {
        const auto id=nextId++;
        objects.emplace(id,Collection{kind,{},{},{}});
        return id;
    } catch(const std::bad_alloc&){return -2;}
}
extern "C" int64_t nq_collection_add(int64_t id,int64_t key,int64_t value) {
    std::lock_guard<std::mutex> guard(mu);
    auto* c=lookup(id);
    if(!c)return -1;
    try {
        if(c->kind==1){
            if(c->list.size()>=maxElements)return -2;
            c->list.push_back(value);
        }else if(c->kind==2){
            if(!c->map.count(key)&&c->map.size()>=maxElements)return -2;
            c->map.insert_or_assign(key,value);
        }else{
            if(!c->set.count(key)&&c->set.size()>=maxElements)return -2;
            c->set.insert(key);
        }
        return 0;
    }catch(const std::bad_alloc&){return -2;}
}
extern "C" int64_t nq_collection_get(int64_t id,int64_t key,int64_t* output) {
    if(!output)return -1;
    std::lock_guard<std::mutex> guard(mu);
    const auto* c=lookup(id);
    if(!c)return -1;
    if(c->kind==1){
        if(key<0||static_cast<std::uint64_t>(key)>=c->list.size())return -3;
        *output=c->list[static_cast<std::size_t>(key)];
    }else if(c->kind==2){
        const auto it=c->map.find(key);
        if(it==c->map.end())return -4;
        *output=it->second;
    }else{
        if(!c->set.count(key))return -4;
        *output=key;
    }
    return 0;
}
extern "C" int64_t nq_collection_size(int64_t id) {
    std::lock_guard<std::mutex> guard(mu);
    const auto* c=lookup(id);
    return c?static_cast<std::int64_t>(sizeOf(*c)):-1;
}
extern "C" int64_t nq_collection_remove(int64_t id,int64_t key) {
    std::lock_guard<std::mutex> guard(mu);
    auto* c=lookup(id);
    if(!c)return -1;
    if(c->kind==1){
        if(key<0||static_cast<std::uint64_t>(key)>=c->list.size())return -3;
        c->list.erase(c->list.begin()+static_cast<std::size_t>(key));return 0;
    }
    if(c->kind==2)return c->map.erase(key)?0:-4;
    return c->set.erase(key)?0:-4;
}
extern "C" int64_t nq_collection_clear(int64_t id) {
    std::lock_guard<std::mutex> guard(mu);
    auto* c=lookup(id);
    if(!c)return -1;
    c->list.clear();c->map.clear();c->set.clear();return 0;
}
extern "C" int64_t nq_collection_close(int64_t id) {
    std::lock_guard<std::mutex> guard(mu);
    return objects.erase(id)?0:-1;
}

// Beginner List<int> runtime. Errors are explicit fatal run-time errors,
// rather than silently dropping appends or allowing use-after-release.
#include <cstdio>
#include <cstdlib>
namespace {
[[noreturn]] void listRuntimeTrap(const char* action,std::int64_t status) {
    std::fprintf(stderr,"Noqeri List<int> %s failed (status %lld)\\n",
                 action,static_cast<long long>(status));
    std::fflush(stderr);
    std::abort();
}
}
extern "C" int64_t nq_list_new(void) {
    auto result=nq_collection_new(1);
    if(result<=0)listRuntimeTrap("allocation",result);
    return result;
}
extern "C" int64_t nq_list_append(int64_t handle,int64_t value) {
    auto status=nq_collection_add(handle,0,value);
    if(status!=0)listRuntimeTrap("append",status);
    return 0;
}
extern "C" int64_t nq_list_length(int64_t handle) {
    auto result=nq_collection_size(handle);
    if(result<0)listRuntimeTrap("length",result);
    return result;
}
extern "C" int64_t nq_list_close(int64_t handle) {
    auto status=nq_collection_close(handle);
    if(status!=0)listRuntimeTrap("close",status);
    return 0;
}
