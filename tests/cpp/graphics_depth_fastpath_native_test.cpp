// Real Noqeri native AOT -> explicit C++ SIMD ABI, Z-depth clear differential.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>
struct NqU32Slice { std::uint32_t* data; std::size_t length; };
extern "C" std::uint64_t nqDepthClearAccelerated(NqU32Slice*,std::size_t,std::size_t);
namespace {
constexpr std::uint32_t sentinel=0x9abcd123u, cleared=0xffffffffu;
void require(bool yes,const char*message){if(!yes)throw std::runtime_error(message);}
}
int main(){
 try {
    std::size_t cases=0;
    for(std::size_t width:std::array<std::size_t,11>{0,1,2,3,7,63,64,65,255,256,4097})
        for(std::size_t height:std::array<std::size_t,11>{0,1,2,3,7,63,64,65,255,256,4097}){
            const std::size_t need=width*height;
            const std::size_t capacity=std::min<std::size_t>(need,65536);
            for(std::size_t validLength:std::array<std::size_t,2>{
                capacity,capacity?capacity-1:0}){
                std::vector<std::uint32_t> actual(capacity+7,sentinel);
                NqU32Slice destination{actual.data(),validLength};
                const bool correct=width!=0&&height!=0&&width<=4096&&height<=4096&&
                    height<=validLength/width;
                const bool result=nqDepthClearAccelerated(&destination,width,height)!=0;
                require(result==correct,"depth clear wrong status");
                for(std::size_t i=0;i<actual.size();++i)
                    require(actual[i]==(correct&&i<need?cleared:sentinel),
                        "depth clear changed padding/tail or wrong depth");
                cases++;
            }
        }
    for(std::size_t size:std::array<std::size_t,3>{1,65536,262144}){
        std::vector<std::uint32_t> actual(size+4,sentinel);
        NqU32Slice destination{actual.data(),actual.size()};
        std::size_t w=size==1?1:(size==65536?256:512);
        std::size_t h=w;
        require(nqDepthClearAccelerated(&destination,w,h)!=0,"depth clear valid frame failed");
        for(std::size_t i=0;i<actual.size();i++)
            require(actual[i]==(i<size?cleared:sentinel),"depth clear guard corruption");
        cases++;
    }
    std::fprintf(stderr,"Noqeri accelerated depth buffer clear: PASS cases=%zu\n",cases);
    return 0;
 }catch(const std::exception&e){
    std::fprintf(stderr,"Noqeri accelerated depth buffer clear: FAIL %s\n",e.what());
    return 1;
 }
}
