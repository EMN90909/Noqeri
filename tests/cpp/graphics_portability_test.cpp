// Cross-platform C++ graphics ABI validation, no Noqeri AOT or video driver.
// The exact same public library and test compile under Clang, GCC, MSVC, NDK.
// Tests all code paths, including opt-in forced scalar (portable fallback).
#include "noqeri/graphics_fastpath.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
constexpr std::uint32_t guard=0xcafedeadu;
void check(bool condition,const char* why) {
    if(!condition)throw std::runtime_error(why);
}
void checkFlat(std::size_t length,std::size_t offset,std::uint32_t color) {
    std::vector<std::uint32_t> pixels(length+offset+4,guard);
    check(nq_graphics_clear_argb32(pixels.data()+offset,length,color)==0,
          "valid contiguous clear failed");
    for(std::size_t i=0;i<pixels.size();++i)
        check(pixels[i]==(i>=offset&&i-offset<length?color:guard),
              "contiguous clear changed padding or used wrong color");
}
void checkPitched(std::size_t width,std::size_t height,
                  std::size_t pad,std::uint32_t color) {
    const std::size_t pitch=width+pad;
    std::vector<std::uint32_t> pixels(pitch*height+4,guard);
    check(nq_graphics_clear_rows_argb32(pixels.data(),pixels.size(),width,
                                        height,pitch,color)==0,
          "valid pitched clear failed");
    for(std::size_t y=0;y<height;++y)
        for(std::size_t x=0;x<pitch;++x)
            check(pixels[y*pitch+x]==(x<width?color:guard),
                  "pitched clear changed a row gap");
    for(std::size_t i=height*pitch;i<pixels.size();++i)
        check(pixels[i]==guard,"pitched clear wrote beyond final row");
}
}
int main() {
    try {
        const char* backend=nq_graphics_clear_backend();
        const char* arch=nq_graphics_cpu_arch();
        const char* os=nq_graphics_host_os();
        check(backend&&arch&&os&&*backend&&*arch&&*os,"missing target identifiers");
        check(std::strcmp(backend,"scalar")==0||std::strcmp(backend,"sse2")==0||
              std::strcmp(backend,"neon")==0,"unexpected architecture dispatch");
#ifdef NOQERI_GRAPHICS_FORCE_SCALAR
        check(std::strcmp(backend,"scalar")==0,"forced scalar build used SIMD");
#endif
        check(nq_graphics_clear_argb32(nullptr,0,9)==0,"empty clear invalid");
        check(nq_graphics_clear_argb32(nullptr,1,9)!=0,"null pixels accepted");
        std::uint32_t single=guard;
        check(nq_graphics_clear_argb32(&single,4096u*4096u+1,9)!=0,
              "oversized color framebuffer accepted");
        check(single==guard,"invalid request changed data");
        check(nq_graphics_clear_rows_argb32(&single,1,0,1,1,2)!=0,
              "zero-width pitched buffer accepted");
        check(nq_graphics_clear_rows_argb32(&single,1,1,1,0,2)!=0,
              "zero pitch accepted");
        check(nq_graphics_clear_rows_argb32(&single,1,2,2,2,2)!=0,
              "insufficient pitched capacity accepted");
        check(single==guard,"failed pitch guard modified memory");
        std::size_t cases=0;
        for(auto count:std::array<std::size_t,22>{
            0,1,2,3,4,5,7,8,15,16,17,31,32,63,64,65,127,255,256,1023,65536,262144})
            for(std::size_t offset:{0u,1u,2u,3u})
                for(std::uint32_t fill:{0u,1u,0x12345678u,0xffffffffu}) {
                    checkFlat(count,offset,fill);
                    ++cases;
                }
        for(std::size_t width:{1u,2u,3u,5u,31u,64u,65u,127u,256u})
            for(std::size_t height:{1u,2u,3u,5u,16u})
                for(std::size_t padding:{0u,1u,3u,7u,15u}) {
                    checkPitched(width,height,padding,0xffaabbccu);
                    ++cases;
                }
        std::printf("Noqeri portable graphics C ABI: PASS cases=%zu os=%s cpu=%s kernel=%s\n",
                    cases,os,arch,backend);
        return 0;
    }catch(const std::exception& ex){
        std::fprintf(stderr,"Noqeri portable graphics C ABI: FAIL %s\n",ex.what());
        return 1;
    }
}
