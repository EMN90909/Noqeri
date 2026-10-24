// Independent native correctness and guarded volatile-framebuffer timing.
// Reference is deliberately scalar/division-based; it is NOT a GPU benchmark.
// All production writes are made by Noqeri's compiled os_framebuffer.noqeri.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" std::uint64_t nqOsFbClear(volatile std::uint32_t*,std::size_t,
                                     std::size_t,std::size_t,std::uint32_t);
extern "C" std::uint64_t nqOsFbChecker(volatile std::uint32_t*,std::size_t,
                                       std::size_t,std::size_t,std::size_t);
namespace {
constexpr std::uint32_t guard=0xdecafbadu;
constexpr std::uint32_t green=0xff00ff00u,red=0xffff0000u;
std::size_t cases=0;
void require(bool condition,const std::string& why){
    if(!condition)throw std::runtime_error(why);
}
bool dimensions(std::size_t w,std::size_t h,std::size_t pitch) {
    return w>0&&h>0&&w<=4096&&h<=4096&&pitch>=w&&pitch<=65536;
}
void check(std::size_t w,std::size_t h,std::size_t pitch,std::size_t cell){
    const std::size_t count=h*pitch+8;
    std::vector<std::uint32_t> actual(count,guard),expected(count,guard);
    bool status=nqOsFbClear(actual.data(),w,h,pitch,0xff123456u)!=0;
    const bool valid=dimensions(w,h,pitch);
    require(status==valid,"clear return status mismatch");
    if(valid)for(std::size_t y=0;y<h;y++)
        for(std::size_t x=0;x<w;x++)expected[y*pitch+x]=0xff123456u;
    require(actual==expected,"clear changed row padding, guard or wrong pixel");
    std::fill(actual.begin(),actual.end(),guard);
    std::fill(expected.begin(),expected.end(),guard);
    status=nqOsFbChecker(actual.data(),w,h,pitch,cell)!=0;
    require(status==(valid&&cell!=0),"checker return status mismatch");
    if(valid&&cell!=0)for(std::size_t y=0;y<h;y++)
        for(std::size_t x=0;x<w;x++)
            expected[y*pitch+x]=((x/cell+y/cell)&1u)?red:green;
    require(actual==expected,"checker changed row padding, guard or wrong tile");
    cases+=2;
}
void correctness(){
    const std::array<std::size_t,12> sizes{0,1,2,3,4,7,15,31,32,63,64,65};
    const std::array<std::size_t,10> cells{0,1,2,3,7,16,31,33,257,
                                            std::numeric_limits<std::size_t>::max()};
    for(auto w:sizes)for(auto h:sizes)
        for(auto pitch:std::array<std::size_t,3>{w,w+1,w+7})
            for(auto cell:cells)check(w,h,pitch,cell);
    for(auto cell:cells) {
        check(257,255,260,cell);
        check(256,257,257,cell);
        check(1,4096,2,cell);
        check(4096,1,4096,cell);
    }
    // Invalid dimensions must fail before any memory access.
    check(4097,1,4097,1);
    check(1,4097,1,1);
    check(1,1,65537,1);
    check(4,3,3,1);
    require(!nqOsFbClear(nullptr,1,1,1,0u),"null clear accepted");
    require(!nqOsFbChecker(nullptr,1,1,1,1),"null checker accepted");
    cases+=2;
    std::fprintf(stderr,"Noqeri native volatile framebuffer differential: PASS cases=%zu\n",cases);
}
void benchmark(){
    constexpr std::size_t w=512,h=512,pitch=520;
    std::vector<std::uint32_t> buffer(pitch*h,guard);
    std::uint64_t consumed=0;
    std::puts("scene,sample,elapsed_ns");
    for(std::size_t cell:{1u,8u,16u,64u,1024u}){
        const std::string scene="checker_cell_"+std::to_string(cell);
        for(int repeat=-5;repeat<30;++repeat){
            const auto start=std::chrono::steady_clock::now();
            require(nqOsFbChecker(buffer.data(),w,h,pitch,cell)!=0,"benchmark checker failed");
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-start).count();
            consumed+=buffer[(repeat+100)%h*pitch+repeat%w+5];
            if(repeat>=0)std::printf("%s,%d,%lld\n",scene.c_str(),repeat,
                                     static_cast<long long>(elapsed));
        }
    }
    std::fprintf(stderr,"volatile framebuffer benchmark consumed=%llu (native CPU only)\n",
                 static_cast<unsigned long long>(consumed));
}
}
int main(int argc,char**argv){
    try {
        correctness();
        if(argc==2&&std::string(argv[1])=="--bench")benchmark();
        return 0;
    }catch(const std::exception&error){
        std::fprintf(stderr,"Noqeri OS framebuffer test failed: %s\n",error.what());
        return 1;
    }
}
