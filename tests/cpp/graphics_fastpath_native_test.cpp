// The real Noqeri AOT compiler produces the accelerated wrapper and calls
// this C++ SIMD backend through an explicit native ABI. No JS/GPU assumptions.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "noqeri/graphics_fastpath.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
struct NqU32Slice { std::uint32_t* data; std::size_t length; };
extern "C" std::uint64_t nqRenderClearAccelerated(NqU32Slice*,std::uint32_t);
extern "C" std::uint64_t nqRenderClearRowsAccelerated(
    NqU32Slice*,std::size_t,std::size_t,std::size_t,std::uint32_t);
namespace {
constexpr std::uint32_t sentinel=0xdedebee7u;
volatile std::uint64_t visible=0;
void require(bool okay,const char* message){
    if(!okay)throw std::runtime_error(message);
}
void correctness(){
    std::size_t verified=0;
    for(std::size_t n : std::array<std::size_t,18>{
        0,1,2,3,4,5,7,15,16,17,31,63,64,65,255,256,65536,262144}){
        for(std::size_t offset:{0u,1u,3u}){
            std::vector<std::uint32_t> actual(n+offset+8,sentinel);
            NqU32Slice slice{actual.data()+offset,n};
            require(nqRenderClearAccelerated(&slice,0xa1b2c3d4u)!=0,"native Noqeri clear status");
            for(std::size_t i=0;i<n+offset+8;++i) {
                const auto expected=i>=offset&&i<offset+n?0xa1b2c3d4u:sentinel;
                require(actual[i]==expected,"native clear clobbered tail or misaligned bytes");
            }
            ++verified;
        }
    }
    NqU32Slice empty{nullptr,0};
    require(nqRenderClearAccelerated(&empty,123u)!=0,"empty slice must succeed");
    std::array<std::uint32_t,20> initial{};
    initial.fill(sentinel);
    for(std::size_t w:{0u,1u,2u,3u,4u,4097u})
        for(std::size_t h:{0u,1u,2u,3u,4097u})
            for(std::size_t pitch:{0u,1u,3u,5u,65537u}){
                auto actual=initial;
                NqU32Slice slice{actual.data(),actual.size()};
                const bool valid=w>0&&h>0&&w<=4096&&h<=4096&&pitch>=w&&
                    pitch<=65536&&h<=slice.length/pitch;
                const bool status=nqRenderClearRowsAccelerated(
                    &slice,w,h,pitch,0x12345678u)!=0;
                require(status==valid,"native pitched clear validation mismatch");
                if(valid)
                    for(std::size_t y=0;y<h;++y)
                        for(std::size_t x=0;x<w;++x)
                            initial[y*pitch+x]=0x12345678u;
                require(std::equal(actual.begin(),actual.end(),initial.begin()),
                    "native pitched clear corrupted guard");
                initial.fill(sentinel);
                ++verified;
            }
    for(std::size_t w:{1u,3u,63u,64u,65u,257u})
        for(std::size_t h:{1u,2u,17u,63u,256u})
            for(std::size_t padding:{0u,1u,3u,7u}) {
                const std::size_t pitch=w+padding;
                std::vector<std::uint32_t> actual(pitch*h+4,sentinel);
                NqU32Slice slice{actual.data(),actual.size()};
                require(nqRenderClearRowsAccelerated(&slice,w,h,pitch,0xff886644u)!=0,
                    "pitched clear rejected valid dimensions");
                for(std::size_t y=0;y<h;++y)
                    for(std::size_t x=0;x<pitch;++x)
                        require(actual[y*pitch+x]==(x<w?0xff886644u:sentinel),
                            "pitched clear touched padding or wrong pixel");
                for(std::size_t j=pitch*h;j<actual.size();++j)
                    require(actual[j]==sentinel,"pitched clear touched tail");
                ++verified;
            }
    require(nq_graphics_clear_argb32(nullptr,0,1u)==0,"null empty C ABI");
    require(nq_graphics_clear_argb32(nullptr,4,1u)<0,"null C ABI accepted");
    require(nq_graphics_clear_rows_argb32(nullptr,0,1,1,1,0)<0,
        "null pitched C ABI accepted");
    std::fprintf(stderr,"Noqeri->C++ SIMD clear correctness: PASS cases=%zu\n",verified);
}
void benchmark(){
    std::puts("scene,sample,elapsed_ns");
    for(const std::size_t side:{256u,512u}){
        std::vector<std::uint32_t> pixels(side*side+2,sentinel);
        NqU32Slice slice{pixels.data(),side*side};
        std::vector<long long> times;
        for(int k=-12;k<60;++k){
            const std::uint32_t color=0xff000000u|static_cast<std::uint32_t>(k+128);
            const auto start=std::chrono::steady_clock::now();
            require(nqRenderClearAccelerated(&slice,color)!=0,"bench clear failed");
            const auto duration=std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now()-start).count();
            visible+=pixels[0]+pixels[side*side-1];
            require(pixels[side*side]==sentinel&&pixels[side*side+1]==sentinel,
                "bench clear crossed guard");
            if(k>=0){
                times.push_back(duration);
                std::printf("clear_%zux%zu,%d,%lld\n",side,side,k,
                    static_cast<long long>(duration));
            }
        }
        std::sort(times.begin(),times.end());
        const auto median=(times[29]+times[30])/2;
        std::fprintf(stderr,"Noqeri native fast clear %zux%zu median=%lld ns target_10000ns=%s, p95=%lld ns\n",
            side,side,median,median<=10000?"MET":"NOT_MET",times[56]);
    }
    std::fprintf(stderr,"Noqeri fast clear observable checksum=%llu\n",
        static_cast<unsigned long long>(visible));
}
}
int main(int argc,char**argv){
    try{
        correctness();
        if(argc>1&&std::string(argv[1])=="--bench")benchmark();
        return 0;
    }catch(const std::exception&e){
        std::fprintf(stderr,"Noqeri fast clear FAIL: %s\n",e.what());
        return 1;
    }
}
