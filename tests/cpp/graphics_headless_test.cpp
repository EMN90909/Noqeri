#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" int noqeri_entry(const void* abi);

namespace {
int presented=0,pumped=0,closed=0;
}

extern "C" int nq_os_renderer_present(uint32_t*argb,uint32_t width,uint32_t height) {
    assert(argb);
    assert(width==32 && height==32);
    assert(argb[0]==0xff000040u);
    assert(argb[31]==0xffff0040u);
    assert(argb[32*31]==0xff00ff40u);
    assert(argb[1023]==0xffffff40u);
    assert(argb[16*32+16]==0xff838340u);
    ++presented;
    return 0;
}
extern "C" int nq_os_renderer_pump(uint32_t duration_ms){
    assert(duration_ms==750u);
    ++pumped;return 0;
}
extern "C" int nq_os_renderer_close(){++closed;return 0;}

int main(){
    assert(noqeri_entry(nullptr)==0);
    assert(presented==1&&pumped==1&&closed==1);
    std::puts("Noqeri-written native CPU pixel renderer and checked OS presentation ABI: PASS");
}
