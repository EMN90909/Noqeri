// SPDX-License-Identifier: Apache-2.0
#include "owned_collections.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>
int main(){
    assert(nq_collection_new(0)==-1);
    const auto list=nq_collection_new(1);
    assert(list>0);
    for(int i=0;i<10000;++i)assert(nq_collection_add(list,0,i)==0);
    assert(nq_collection_size(list)==10000);
    std::int64_t value=-1;
    assert(nq_collection_get(list,9999,&value)==0 && value==9999);
    assert(nq_collection_get(list,-1,&value)==-3);
    assert(nq_collection_get(list,10000,&value)==-3);
    assert(nq_collection_remove(list,0)==0);
    assert(nq_collection_get(list,0,&value)==0 && value==1);
    assert(nq_collection_clear(list)==0 && nq_collection_size(list)==0);
    assert(nq_collection_close(list)==0);
    assert(nq_collection_close(list)==-1 && nq_collection_add(list,0,42)==-1);

    const auto map=nq_collection_new(2);
    assert(map>list);
    for(int i=0;i<10000;++i)assert(nq_collection_add(map,i,i*3)==0);
    assert(nq_collection_add(map,3,101)==0);
    assert(nq_collection_size(map)==10000);
    assert(nq_collection_get(map,3,&value)==0 && value==101);
    assert(nq_collection_get(map,10001,&value)==-4);
    assert(nq_collection_remove(map,3)==0 && nq_collection_remove(map,3)==-4);
    assert(nq_collection_close(map)==0);

    const auto set=nq_collection_new(3);
    for(int i=0;i<10000;++i)assert(nq_collection_add(set,i,0)==0);
    for(int i=0;i<10000;++i)assert(nq_collection_add(set,i,0)==0);
    assert(nq_collection_size(set)==10000);
    assert(nq_collection_get(set,42,&value)==0 && value==42);
    assert(nq_collection_remove(set,42)==0 && nq_collection_get(set,42,&value)==-4);
    assert(nq_collection_close(set)==0);

    constexpr int threads=4;
    std::vector<std::thread> pool;
    for(int j=0;j<threads;++j)pool.emplace_back([] {
        const auto local=nq_collection_new(1);
        assert(local>0);
        for(int i=0;i<1000;++i)assert(nq_collection_add(local,0,i)==0);
        assert(nq_collection_size(local)==1000);
        assert(nq_collection_close(local)==0);
    });
    for(auto& t:pool)t.join();
}
