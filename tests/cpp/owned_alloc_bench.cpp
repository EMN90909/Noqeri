// Local-only performance workload: independently validate every result
// before recording timing. It is not part of CTest correctness qualification.
#include "owned_collections.h"
#include "owned_value_list.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
bool good(std::int64_t status){return status==0;}
std::int64_t run(const std::string& mode){
    std::int64_t checksum=0;
    if(mode=="int-list"){
        const auto id=nq_collection_new(1);
        if(id<=0)return -1;
        for(int i=0;i<200000;++i)
            if(!good(nq_collection_add(id,0,i)))return -1;
        for(int i=0;i<200000;i+=13){
            std::int64_t value=0;
            if(!good(nq_collection_get(id,i,&value))||value!=i)return -1;
            checksum+=value;
        }
        if(!good(nq_collection_close(id)))return -1;
    }else if(mode=="owned-value"){
        const auto id=nq_value_list_new_copy(32,8);
        if(id<=0)return -1;
        for(std::uint64_t i=0;i<60000;++i){
            const std::array<std::uint64_t,4> item{i,i^0xfedcba98u,i+9,~i};
            if(!good(nq_value_list_append(id,item.data())))return -1;
        }
        for(std::uint64_t i=0;i<60000;i+=7){
            std::array<std::uint64_t,4> result{};
            if(!good(nq_value_list_get(id,static_cast<std::int64_t>(i),
                                      result.data())))return -1;
            if(result[0]!=i||result[2]!=i+9)return -1;
            checksum+=static_cast<std::int64_t>(result[0]);
        }
        if(!good(nq_value_list_close(id)))return -1;
    }else if(mode=="handles"){
        for(int i=0;i<30000;++i){
            const auto id=nq_collection_new(1);
            if(id<=0||!good(nq_collection_close(id)))return -1;
            ++checksum;
        }
    }else if(mode=="threaded"){
        std::array<std::int64_t,4> results{};
        std::vector<std::thread> ts;
        for(std::size_t worker=0;worker<4;++worker)ts.emplace_back([&,worker]{
            const auto id=nq_collection_new(1);
            if(id<=0){results[worker]=-1;return;}
            for(int i=0;i<15000;++i)
                if(!good(nq_collection_add(id,0,i))){
                    results[worker]=-1;return;
                }
            const auto len=nq_collection_size(id);
            if(len!=15000||!good(nq_collection_close(id))){
                results[worker]=-1;return;
            }
            results[worker]=len;
        });
        for(auto& t:ts)t.join();
        for(const auto n:results){if(n<0)return -1;checksum+=n;}
    }else return -1;
    return checksum;
}
}
int main(int argc,char**argv){
    if(argc!=2)return 2;
    const auto result=run(argv[1]);
    if(result<0){std::cerr<<"allocation benchmark correctness failure\n";return 1;}
    std::cout<<result<<"\n";
    return 0;
}
