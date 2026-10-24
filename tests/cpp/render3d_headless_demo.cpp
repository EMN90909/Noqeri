#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
extern "C" int noqeri_entry(const void* abi);
static int presented=0,pumped=0,closed=0;
extern "C" int nq_os_renderer_present(const std::uint32_t* pixels,std::size_t n,std::uint32_t w,std::uint32_t h){
    assert(pixels && n==4096 && w==64 && h==64);
    assert(pixels[0]==0xff111122u);
    assert(pixels[8*64+32]==0xffff0000u);
    assert(pixels[24*64+32]==0xff00ff00u);
    ++presented; return 0;
}
extern "C" int nq_os_renderer_pump(std::uint32_t ms){assert(ms==500);++pumped;return 0;}
extern "C" int nq_os_renderer_close(){++closed;return 0;}
int main(){
    assert(noqeri_entry(nullptr)==0);
    assert(presented==1 && pumped==1 && closed==1);
    std::puts("Noqeri native 3D occlusion + OS presentation ABI: PASS");
}
