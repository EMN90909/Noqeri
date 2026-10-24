#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

extern "C" std::uint64_t nqGpuFwRead32(volatile std::uint32_t*,std::size_t,std::size_t,std::uint32_t*);
extern "C" std::uint64_t nqGpuFwWrite32(volatile std::uint32_t*,std::size_t,std::size_t,std::uint32_t);
extern "C" std::uint64_t nqGpuFwPollEqual32(volatile std::uint32_t*,std::size_t,std::size_t,std::uint32_t,std::size_t);
extern "C" std::uint64_t nqGpuFwSimStep(volatile std::uint32_t*,std::size_t);

int main() {
  std::array<std::uint32_t,9> page{};
  page.fill(0xdeadbeefu);
  volatile std::uint32_t* mapped=page.data();
  std::uint32_t result=0xabcdef12u;
  assert(nqGpuFwRead32(mapped,4,1,&result)==1 && result==0xdeadbeefu);
  assert(nqGpuFwRead32(mapped,4,4,&result)==0 && result==0xdeadbeefu);
  assert(nqGpuFwRead32(nullptr,4,0,&result)==0);
  assert(nqGpuFwRead32(mapped,4,0,nullptr)==0);
  assert(nqGpuFwRead32(mapped,0,0,&result)==0);
  assert(nqGpuFwRead32(mapped,4097,0,&result)==0);
  assert(nqGpuFwWrite32(mapped,4,1,0x43u)==1 && page[1]==0x43u);
  assert(nqGpuFwWrite32(mapped,4,4,9)==0 && page[4]==0xdeadbeefu);
  assert(nqGpuFwWrite32(nullptr,4,0,7)==0);
  assert(nqGpuFwPollEqual32(mapped,4,1,0x43u,1)==1);
  assert(nqGpuFwPollEqual32(mapped,4,1,0x42u,3)==0);
  assert(nqGpuFwPollEqual32(mapped,4,1,0x43u,0)==0);
  assert(nqGpuFwPollEqual32(mapped,4,1,0x43u,4097)==0);
  assert(nqGpuFwPollEqual32(mapped,4,4,0x43u,1)==0);
  const auto snapshot=page;
  assert(nqGpuFwSimStep(mapped,3)==0 && page==snapshot);
  assert(nqGpuFwSimStep(mapped,4)==0 && page==snapshot);
  page[0]=0x4e514657u;
  page[1]=0u;
  page[2]=9u;
  page[3]=12u;
  assert(nqGpuFwSimStep(mapped,4)==0 && page[1]==0 && page[3]==12u);
  page[2]=1u;
  assert(nqGpuFwSimStep(mapped,4)==1 && page[1]==1 && page[3]==13u);
  page[1]=0u;
  page[2]=2u;
  assert(nqGpuFwSimStep(mapped,4)==1 && page[1]==1 && page[3]==0xfffffff2u);
  for(std::size_t i=4;i<page.size();++i)assert(page[i]==0xdeadbeefu);
  std::puts("Noqeri GPU firmware simulation: PASS bounded MMIO, poll, mailbox, sentinels");
}
