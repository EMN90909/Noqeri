// SPDX-License-Identifier: Apache-2.0
#include "owned_value_list.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

namespace {
constexpr std::size_t maxElements = 1'000'000;
constexpr std::size_t maxBytes = 128 * 1024 * 1024;
std::mutex guard;
struct List {
    std::size_t width=0,alignment=0,stride=0,length=0,capacity=0;
    std::byte* buffer=nullptr;
    nq_value_clone_fn clone=nullptr;
    nq_value_drop_fn drop=nullptr;
    void* context=nullptr;
    std::byte* at(std::size_t i)const {return buffer+i*stride;}
    bool aliases(const void* p)const {
        if(!buffer || !p)return false;
        const auto addr=reinterpret_cast<std::uintptr_t>(p);
        const auto start=reinterpret_cast<std::uintptr_t>(buffer);
        return addr>=start && addr-start<length*stride;
    }
    int copyAt(void* dest,const void* src)const {
        if(clone) {try{return clone(dest,src,context)==0?0:-6;}catch(...){return -6;}}
        std::memcpy(dest,src,width);
        return 0;
    }
    void destroy(void* p)const {
        if(drop){try{drop(p,context);}catch(...){/* drop callbacks may not throw */}}
    }
    void freeAll() {
        for(std::size_t i=0;i<length;i++)destroy(at(i));
        ::operator delete(buffer,std::align_val_t(alignment));
        buffer=nullptr;length=0;capacity=0;
    }
    ~List(){freeAll();}
    List()=default;
    List(const List&)=delete;
    List& operator=(const List&)=delete;
};
std::map<std::int64_t,std::unique_ptr<List>> entries;
std::int64_t nextHandle=1;
List* find(std::int64_t id){auto i=entries.find(id);return i==entries.end()?nullptr:i->second.get();}
std::byte* allocate(const List& l,std::size_t cap) {
    if(cap==0)return nullptr;
    if(cap>maxElements || cap>maxBytes/l.stride)return nullptr;
    return static_cast<std::byte*>(::operator new(cap*l.stride,std::align_val_t(l.alignment),std::nothrow));
}
void discardClones(const List& l,std::byte* ptr,std::size_t count) {
    if(!ptr)return;
    if(l.drop)for(std::size_t i=0;i<count;i++)l.destroy(ptr+i*l.stride);
    ::operator delete(ptr,std::align_val_t(l.alignment));
}
std::int64_t reserve(List& l,std::size_t capacity) {
    if(capacity<=l.capacity)return 0;
    if(capacity>maxElements || capacity>maxBytes/l.stride)return -2;
    auto* replacement=allocate(l,capacity);
    if(!replacement)return -2;
    std::size_t copied=0;
    for(;copied<l.length;copied++) {
        if(l.copyAt(replacement+copied*l.stride,l.at(copied))!=0) {
            discardClones(l,replacement,copied);return -6;
        }
    }
    l.freeAll();
    l.buffer=replacement;l.capacity=capacity;l.length=copied;
    return 0;
}
std::int64_t create(std::uint64_t size,std::uint64_t align,
                    nq_value_clone_fn clone,nq_value_drop_fn drop,void* ctx) {
    if(size==0||size>65536||align==0||align>4096||(align&(align-1))||
       (clone==nullptr)!=(drop==nullptr))return -5;
    const auto a=std::max<std::size_t>(align,alignof(std::max_align_t));
    if(size>std::numeric_limits<std::size_t>::max()-(a-1))return -5;
    auto l=std::make_unique<List>();
    l->width=static_cast<std::size_t>(size);
    l->alignment=a;
    l->stride=((l->width+a-1)/a)*a;
    l->clone=clone;l->drop=drop;l->context=ctx;
    if(nextHandle==std::numeric_limits<std::int64_t>::max())return -2;
    const auto id=nextHandle++;
    entries.emplace(id,std::move(l));
    return id;
}
}
extern "C" std::int64_t nq_value_list_new_copy(std::uint64_t size,std::uint64_t align) {
    std::lock_guard<std::mutex> lock(guard);
    try{return create(size,align,nullptr,nullptr,nullptr);}catch(...){return -2;}
}
extern "C" std::int64_t nq_value_list_new_custom(std::uint64_t size,std::uint64_t align,
    nq_value_clone_fn clone,nq_value_drop_fn drop,void* context) {
    std::lock_guard<std::mutex> lock(guard);
    try{return create(size,align,clone,drop,context);}catch(...){return -2;}
}
extern "C" std::int64_t nq_value_list_append(std::int64_t id,const void* element) {
    std::lock_guard<std::mutex> lock(guard);
    auto* l=find(id);
    if(!l)return -1;
    if(!element || l->aliases(element))return -5;
    if(l->length==maxElements)return -2;
    if(l->length==l->capacity){
        const auto newCap=l->capacity==0?4:std::min(maxElements,l->capacity*2);
        const auto status=reserve(*l,newCap);
        if(status!=0)return status;
    }
    const auto status=l->copyAt(l->at(l->length),element);
    if(status!=0)return status;
    ++l->length;return 0;
}
extern "C" std::int64_t nq_value_list_get(std::int64_t id,std::int64_t index,void* out) {
    std::lock_guard<std::mutex> lock(guard);
    auto* l=find(id);
    if(!l)return -1;
    if(index<0||static_cast<std::uint64_t>(index)>=l->length)return -3;
    if(!out||l->aliases(out))return -5;
    return l->copyAt(out,l->at(static_cast<std::size_t>(index)));
}
extern "C" std::int64_t nq_value_list_remove(std::int64_t id,std::int64_t index) {
    std::lock_guard<std::mutex> lock(guard);
    auto* l=find(id);
    if(!l)return -1;
    if(index<0||static_cast<std::uint64_t>(index)>=l->length)return -3;
    auto at=static_cast<std::size_t>(index);
    if(!l->clone) {
        if(l->length-at>1)std::memmove(l->at(at),l->at(at+1),(l->length-at-1)*l->stride);
        --l->length;return 0;
    }
    // Nontrivial elements must be CLONED into a new allocation; relocating
    // their object representations with memmove is not lifetime-safe.
    auto* next=allocate(*l,l->capacity);
    if(!next)return -2;
    std::size_t copied=0;
    for(std::size_t i=0;i<l->length;i++)if(i!=at) {
        if(l->copyAt(next+copied*l->stride,l->at(i))!=0) {
            discardClones(*l,next,copied);return -6;
        }
        ++copied;
    }
    const auto cap=l->capacity;
    l->freeAll();l->buffer=next;l->capacity=cap;l->length=copied;
    return 0;
}
extern "C" std::int64_t nq_value_list_size(std::int64_t id) {
    std::lock_guard<std::mutex> lock(guard);
    const auto* l=find(id);
    return l?static_cast<std::int64_t>(l->length):-1;
}
extern "C" std::int64_t nq_value_list_clear(std::int64_t id) {
    std::lock_guard<std::mutex> lock(guard);
    auto* l=find(id);if(!l)return -1;
    for(std::size_t i=0;i<l->length;i++)l->destroy(l->at(i));
    l->length=0;return 0;
}
extern "C" std::int64_t nq_value_list_close(std::int64_t id) {
    std::lock_guard<std::mutex> lock(guard);
    return entries.erase(id)?0:-1;
}
