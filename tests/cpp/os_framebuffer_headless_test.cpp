#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>

extern "C" int noqeri_entry(const void* abi);
static uint32_t pixels[18];
extern "C" uint32_t* nq_os_framebuffer_memory(){return pixels;}
int main(){
    for(auto&pixel:pixels)pixel=0xdecafbadu;
    assert(noqeri_entry(nullptr)==0);
    // Green/Red checked pattern, including the row padding invariant.
    assert(pixels[0]==0xff00ff00u);
    assert(pixels[1]==0xff00ff00u);
    assert(pixels[2]==0xffff0000u);
    assert(pixels[3]==0xffff0000u);
    assert(pixels[6]==0xff00ff00u);
    assert(pixels[8]==0xffff0000u);
    assert(pixels[12]==0xffff0000u);
    assert(pixels[14]==0xff00ff00u);
    assert(pixels[4]==0xdecafbadu);
    assert(pixels[5]==0xdecafbadu);
    assert(pixels[10]==0xdecafbadu);
    assert(pixels[11]==0xdecafbadu);
    assert(pixels[16]==0xdecafbadu);
    assert(pixels[17]==0xdecafbadu);
    std::puts("Noqeri native OS/framebuffer pitch and volatile pixel writes: PASS");
}
