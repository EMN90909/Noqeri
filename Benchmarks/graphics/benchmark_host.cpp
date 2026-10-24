// Identical host for C and Noqeri. All rendering work is outside this file.
// Output timings measure native CPU pixel generation, NOT GPU/SDL presentation.
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" int noqeri_entry(const void* abi);
extern "C" int c_render_entry(const void* abi);

namespace {
size_t g_width=0,g_height=0,g_frames=0;
int g_mode=0;
std::vector<uint32_t> g_pixels;
uint64_t g_digest=1469598103934665603ull;
std::chrono::steady_clock::time_point g_start;
uint64_t g_elapsed_ns=0;
uint64_t g_markers=0;
}

extern "C" uint32_t* nq_bench_framebuffer(){return g_pixels.data();}
extern "C" size_t nq_bench_width(){return g_width;}
extern "C" size_t nq_bench_height(){return g_height;}
extern "C" size_t nq_bench_frames(){return g_frames;}
extern "C" int nq_bench_mode(){return g_mode;}
extern "C" void nq_bench_clock_start(){g_start=std::chrono::steady_clock::now();}
extern "C" void nq_bench_clock_stop(){
    const auto now=std::chrono::steady_clock::now();
    g_elapsed_ns=(uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        now-g_start).count();
}
extern "C" void nq_bench_frame_marker(uint32_t* pixels,size_t count){
    if(!pixels || count!=g_pixels.size())std::abort();
    // Sample seven distinct positions after every frame. The changing digest
    // makes the frame observable without hashing millions of pixels each pass.
    for(size_t i=0;i<7;i++){
        size_t index=(i*(count-1))/6;
        g_digest=(g_digest^((uint64_t)pixels[index]+i)) * 1099511628211ull;
    }
    g_markers++;
}
extern "C" void nq_bench_fail(){std::abort();}

static void dump_ppm(const std::string& path){
    std::ofstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot create "+path);
    file<<"P6\n"<<g_width<<" "<<g_height<<"\n255\n";
    for(uint32_t pixel:g_pixels){
        char rgb[3]={(char)((pixel>>16)&0xff),(char)((pixel>>8)&0xff),
                     (char)(pixel&0xff)};
        file.write(rgb,3);
    }
    if(!file)throw std::runtime_error("Cannot write "+path);
}
int main(int argc,char** argv){
    try{
        if(argc<5 || argc>6){
            std::fprintf(stderr,"usage: render <gradient|checker> W H FRAMES [output.ppm]\n");
            return 2;
        }
        const std::string mode=argv[1];
        if(mode!="gradient"&&mode!="checker")return 2;
        g_mode=mode=="gradient"?0:1;
        g_width=std::stoull(argv[2]);
        g_height=std::stoull(argv[3]);
        g_frames=std::stoull(argv[4]);
        if(g_width==0||g_height==0||g_width>4096||g_height>4096||
           g_width*g_height>4096ull*4096ull||g_frames==0||g_frames>10000){
            throw std::runtime_error("Invalid graphics benchmark dimensions/count");
        }
        g_pixels.assign(g_width*g_height,0u);
#ifdef BENCH_C_REFERENCE
        int result=c_render_entry(nullptr);
#else
        int result=noqeri_entry(nullptr);
#endif
        if(result!=0||g_markers!=g_frames||g_elapsed_ns==0)
            throw std::runtime_error("Rendering did not complete");
        if(argc==6)dump_ppm(argv[5]);
        std::printf("{\"workload\":\"%s\",\"width\":%zu,\"height\":%zu,"
                    "\"frames\":%zu,\"elapsed_ns\":%" PRIu64 ","
                    "\"frame_markers\":%" PRIu64 ",\"digest\":%" PRIu64 "}\n",
                    mode.c_str(),g_width,g_height,g_frames,g_elapsed_ns,
                    g_markers,g_digest);
        return 0;
    }catch(const std::exception& error){
        std::fprintf(stderr,"benchmark error: %s\n",error.what());return 1;
    }
}
