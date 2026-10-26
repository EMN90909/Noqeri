// SPDX-License-Identifier: Apache-2.0
#include "owned_value_map.h"
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace {
constexpr std::size_t maxCount=1'000'000,maxStorage=128*1024*1024;
struct Map {
    std::size_t keySize=0,valueSize=0;
    bool set=false;
    std::map<std::string,std::string> data;
};
std::mutex mutex;
std::map<std::int64_t,std::unique_ptr<Map>> registry;
std::int64_t nextId=1;
Map* find(std::int64_t id,bool set){
    auto it=registry.find(id);
    return it!=registry.end()&&it->second->set==set?it->second.get():nullptr;
}
bool widthOK(std::uint64_t width,std::uint64_t alignment){
    return width>0&&width<=65536&&alignment>0&&alignment<=4096&&
        (alignment&(alignment-1))==0;
}
std::int64_t allocate(std::uint64_t kw,std::uint64_t ka,std::uint64_t vw,
                      std::uint64_t va,bool set){
    if(!widthOK(kw,ka)||(!set&&!widthOK(vw,va)))return -5;
    if(nextId==INT64_MAX)return -2;
    try{
        auto m=std::make_unique<Map>();
        m->keySize=static_cast<std::size_t>(kw);
        m->valueSize=set?0:static_cast<std::size_t>(vw);
        m->set=set;
        const auto id=nextId++;
        registry.emplace(id,std::move(m));
        return id;
    }catch(...){return -2;}
}
bool fits(const Map& m){
    const std::size_t width=m.keySize+m.valueSize+72;
    return m.data.size()<maxCount&&m.data.size()<maxStorage/width;
}
std::string keyBytes(const Map& m,const void* key){
    return {static_cast<const char*>(key),m.keySize};
}
}
extern "C" std::int64_t nq_value_map_new_copy(uint64_t kw,uint64_t ka,uint64_t vw,uint64_t va) {
    std::lock_guard<std::mutex> lock(mutex);
    return allocate(kw,ka,vw,va,false);
}
extern "C" std::int64_t nq_value_set_new_copy(uint64_t kw,uint64_t ka) {
    std::lock_guard<std::mutex> lock(mutex);
    return allocate(kw,ka,0,1,true);
}
extern "C" std::int64_t nq_value_map_put(std::int64_t id,const void* key,const void* value) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,false);if(!m)return -1;
    if(!key||!value)return -5;
    try{
        const auto k=keyBytes(*m,key);
        if(!m->data.count(k)&&!fits(*m))return -2;
        m->data.insert_or_assign(k,std::string(static_cast<const char*>(value),m->valueSize));
        return 0;
    }catch(...){return -2;}
}
extern "C" std::int64_t nq_value_map_get(std::int64_t id,const void* key,void* out) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,false);if(!m)return -1;
    if(!key||!out)return -5;
    const auto it=m->data.find(keyBytes(*m,key));
    if(it==m->data.end())return -4;
    std::memcpy(out,it->second.data(),m->valueSize);
    return 0;
}
extern "C" std::int64_t nq_value_map_remove(std::int64_t id,const void* key) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,false);if(!m)return -1;
    if(!key)return -5;
    return m->data.erase(keyBytes(*m,key))?0:-4;
}
extern "C" std::int64_t nq_value_map_size(std::int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,false);return m?static_cast<std::int64_t>(m->data.size()):-1;
}
extern "C" std::int64_t nq_value_map_close(std::int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,false);if(!m)return -1;
    registry.erase(id);return 0;
}
extern "C" std::int64_t nq_value_set_add(std::int64_t id,const void* value) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,true);if(!m)return -1;
    if(!value)return -5;
    try{
        const auto key=keyBytes(*m,value);
        if(!m->data.count(key)&&!fits(*m))return -2;
        m->data.emplace(key,std::string());
        return 0;
    }catch(...){return -2;}
}
extern "C" std::int64_t nq_value_set_has(std::int64_t id,const void* value) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,true);if(!m)return -1;
    if(!value)return -5;
    return m->data.count(keyBytes(*m,value))?1:0;
}
extern "C" std::int64_t nq_value_set_remove(std::int64_t id,const void* value) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,true);if(!m)return -1;
    if(!value)return -5;
    return m->data.erase(keyBytes(*m,value))?0:-4;
}
extern "C" std::int64_t nq_value_set_size(std::int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,true);return m?static_cast<std::int64_t>(m->data.size()):-1;
}
extern "C" std::int64_t nq_value_set_close(std::int64_t id) {
    std::lock_guard<std::mutex> lock(mutex);
    auto* m=find(id,true);if(!m)return -1;
    registry.erase(id);return 0;
}
