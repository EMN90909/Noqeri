// Bitwise differential qualification and raw-frame CPU timing for native
// Noqeri raster kernels. C++ is the independent reference, not a pixel renderer
// used by production Noqeri. No GPU or SDL work is measured in this file.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

struct NqSliceU32 { std::uint32_t* data; std::size_t length; };
extern "C" std::uint64_t nqRenderGradient(NqSliceU32*, std::size_t, std::size_t);
extern "C" std::uint64_t nqRenderChecker(NqSliceU32*, std::size_t, std::size_t,
                                         std::size_t, std::uint32_t, std::uint32_t);
extern "C" std::uint64_t nqRenderClear(NqSliceU32*, std::uint32_t);

namespace {
volatile std::uint64_t consumed = 0;
constexpr std::uint32_t initial = 0xdefaced1u;
constexpr std::uint32_t first = 0xff00ff00u;
constexpr std::uint32_t second = 0xffff0000u;
void require(bool valid, const std::string& message) {
    if (!valid) throw std::runtime_error(message);
}

bool referenceGradient(std::uint32_t* dest,std::size_t length,
                       std::size_t w,std::size_t h) {
    if (!w||!h||w>4096||h>4096||h>length/w)return false;
    for(std::size_t y=0;y<h;++y)for(std::size_t x=0;x<w;++x) {
        const std::uint32_t red=w>1 ? static_cast<std::uint32_t>(x*255/(w-1)):0;
        const std::uint32_t green=h>1 ? static_cast<std::uint32_t>(y*255/(h-1)):0;
        dest[y*w+x]=0xff000040u+(red<<16)+(green<<8);
    }
    return true;
}
bool referenceChecker(std::uint32_t* dest,std::size_t length,
                      std::size_t w,std::size_t h,std::size_t cell) {
    if (!w||!h||!cell||h>length/w)return false;
    for(std::size_t y=0;y<h;++y)for(std::size_t x=0;x<w;++x)
        dest[y*w+x]=((x/cell+y/cell)&1u)?second:first;
    return true;
}
bool referenceClear(std::uint32_t* dest,std::size_t length) {
    std::fill_n(dest,length,0xff345678u);
    return true;
}
void compare(const char* scene,std::size_t w,std::size_t h,std::size_t cell,
             std::size_t actualLength) {
    const std::size_t allocated=std::max<std::size_t>(w*h+3,actualLength+3);
    std::vector<std::uint32_t> nq(allocated,initial),reference(allocated,initial);
    NqSliceU32 pixels{nq.data(),actualLength};
    bool expected=false,got=false;
    if(std::strcmp(scene,"gradient")==0){
        expected=referenceGradient(reference.data(),actualLength,w,h);
        got=nqRenderGradient(&pixels,w,h)!=0;
    }else if(std::strcmp(scene,"checker")==0){
        expected=referenceChecker(reference.data(),actualLength,w,h,cell);
        got=nqRenderChecker(&pixels,w,h,cell,first,second)!=0;
    }else{
        expected=referenceClear(reference.data(),actualLength);
        got=nqRenderClear(&pixels,0xff345678u)!=0;
    }
    if(got!=expected)
        throw std::runtime_error(std::string(scene)+" returned wrong status w="+
            std::to_string(w)+" h="+std::to_string(h)+" cell="+std::to_string(cell));
    for(std::size_t i=0;i<allocated;i++)
        if(nq[i]!=reference[i])
            throw std::runtime_error(std::string(scene)+" differs at pixel "+std::to_string(i)+
                " w="+std::to_string(w)+" h="+std::to_string(h)+
                " cell="+std::to_string(cell));
}
void correctness() {
    const std::array<std::size_t,15> dims{0,1,2,3,7,15,31,63,64,65,127,255,256,257,513};
    const std::array<std::size_t,10> cells{
        0,1,2,3,7,16,31,33,257,std::numeric_limits<std::size_t>::max()
    };
    std::size_t verified=0;
    for(std::size_t w:dims)for(std::size_t h:dims){
        const std::size_t need=w*h;
        compare("gradient",w,h,0,need);++verified;
        if(need) {compare("gradient",w,h,0,need-1);++verified;}
        for(std::size_t cell:cells){
            compare("checker",w,h,cell,need);++verified;
            if(need){compare("checker",w,h,cell,need-1);++verified;}
        }
    }
    for(std::size_t n:{0u,1u,2u,3u,7u,31u,63u,64u,65u,255u,256u,257u,4096u,65536u}){
        compare("clear",n,1,0,n);++verified;
    }
    for(const auto& pair:std::array<std::array<std::size_t,2>,6>{{
        {4096,1},{1,4096},{4096,2},{2,4096},{511,513},{512,512}
    }}){
        compare("gradient",pair[0],pair[1],0,pair[0]*pair[1]);++verified;
        compare("checker",pair[0],pair[1],16,pair[0]*pair[1]);++verified;
    }
    std::cout<<"Noqeri native raster exact C-reference differential: PASS cases="<<verified<<"\n";
}

struct Scene {
    const char* name;
    std::size_t w,h,cell;
};
void benchmark(const Scene& scene) {
    const std::size_t n=scene.w*scene.h;
    std::vector<std::uint32_t> nq(n,initial), ref(n,initial);
    NqSliceU32 slice{nq.data(),n};
    auto drawNoqeri=[&] {
        if(std::strcmp(scene.name,"gradient")==0)return nqRenderGradient(&slice,scene.w,scene.h)!=0;
        if(std::strcmp(scene.name,"checker")==0)
            return nqRenderChecker(&slice,scene.w,scene.h,scene.cell,first,second)!=0;
        return nqRenderClear(&slice,0xff345678u)!=0;
    };
    auto drawC=[&] {
        if(std::strcmp(scene.name,"gradient")==0)
            return referenceGradient(ref.data(),n,scene.w,scene.h);
        if(std::strcmp(scene.name,"checker")==0)
            return referenceChecker(ref.data(),n,scene.w,scene.h,scene.cell);
        return referenceClear(ref.data(),n);
    };
    // Use isolated timers for raster only, with consumed outputs between
    // samples so the reference optimizer may not eliminate dead renders.
    for(int warm=0;warm<5;++warm){
        require(drawNoqeri()&&drawC(),"failed warmup");
        require(nq==ref,"pixel mismatch during warmup");
        consumed=consumed+nq[(warm*37u)%n];
    }
    for(int sample=0;sample<30;++sample){
        const bool reverse=(sample&1)!=0;
        for(int pass=0;pass<2;++pass){
            const bool isNoqeri=(pass==0)^reverse;
            const auto before=std::chrono::steady_clock::now();
            const bool ok=isNoqeri?drawNoqeri():drawC();
            const auto after=std::chrono::steady_clock::now();
            require(ok,"render rejected benchmark dimensions");
            auto& buffer=isNoqeri?nq:ref;
            consumed=consumed+buffer[(sample*191u+pass)%n];
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(after-before).count();
            std::cout<<scene.name<<','<<scene.w<<'x'<<scene.h<<','
                     <<(isNoqeri?"noqeri":"c")<<','<<sample<<','<<ns<<'\n';
        }
        require(nq==ref,"post-sample buffer mismatch");
    }
}
} // namespace

int main(int argc,char**argv){
    try {
        correctness();
        if(argc>1&&std::string(argv[1])=="--bench"){
            std::cout<<"scene,resolution,engine,sample,render_ns\n";
            for(const Scene scene:std::array<Scene,9>{{
                {"gradient",64,64,0},{"gradient",256,256,0},{"gradient",512,512,0},
                {"checker",64,64,16},{"checker",256,256,16},{"checker",512,512,16},
                {"clear",64,64,0},{"clear",256,256,0},{"clear",512,512,0}
            }})benchmark(scene);
            std::cerr<<"benchmark output sink="<<consumed<<"\n";
        }
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL graphics differential: "<<e.what()<<'\n';
        return 1;
    }
}
