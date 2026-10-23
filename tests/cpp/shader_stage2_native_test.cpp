#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int noqeri_entry(const void* abi);
extern "C" int nq_shader_write(const std::uint8_t *data,std::size_t size) {
  if(!data||size<100||size>8192){std::fputs("invalid Stage-2 WGSL size\n",stderr);return -1;}
  if(std::fwrite(data,1,size,stdout)!=size)return -1;
  return 0;
}
int main() {return noqeri_entry(nullptr);}
