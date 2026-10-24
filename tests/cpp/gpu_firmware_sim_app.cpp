#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
extern "C" int noqeri_entry(const void*);
static std::uint32_t regs[4]={0x4e514657u,0u,1u,42u};
extern "C" volatile std::uint32_t* nq_gpu_mock_registers(){return regs;}
int main() {
    assert(noqeri_entry(nullptr)==0);
    assert(regs[0]==0x4e514657u);
    assert(regs[1]==1u);
    assert(regs[2]==1u);
    assert(regs[3]==43u);
    std::puts("Noqeri GPU firmware simulated source entry: PASS");
}
