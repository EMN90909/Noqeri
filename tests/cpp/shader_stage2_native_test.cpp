#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int noqeri_entry(const void* abi);
extern "C" int nq_shader_inspect(const std::uint8_t *data,std::size_t size) {
  if(!data || size>1024) return -1;
  std::fputs("Stage-2 fixture source: ",stderr);
  std::fwrite(data,1,size,stderr);
  std::fputs("\\n",stderr);
  return 0;
}
extern "C" int nq_shader_error(std::int64_t code) {
  std::fprintf(stderr,"Noqeri Stage-2 shader emitter failed with status %lld\n",
               static_cast<long long>(code));
  std::exit(1);
}
extern "C" int nq_shader_write(const std::uint8_t *data,std::size_t size) {
  if(!data||size<100||size>8192){std::fputs("invalid Stage-2 WGSL size\n",stderr);return -1;}
  if(std::fwrite(data,1,size,stdout)!=size)return -1;
  return 0;
}
int main() {return noqeri_entry(nullptr);}
