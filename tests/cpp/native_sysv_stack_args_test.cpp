#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>

extern "C" std::int64_t nativeSum8(
    std::int64_t,std::int64_t,std::int64_t,std::int64_t,
    std::int64_t,std::int64_t,std::int64_t,std::int64_t);
extern "C" std::int64_t nativeSum7(
    std::int64_t,std::int64_t,std::int64_t,std::int64_t,
    std::int64_t,std::int64_t,std::int64_t);
extern "C" std::int64_t nativeInvoke8();
extern "C" std::int64_t nativeInvoke7();
int main() {
    assert(nativeSum7(1,2,3,4,5,6,7)==28);
    assert(nativeSum8(1,2,3,4,5,6,7,8)==36);
    assert(nativeInvoke7()==28);
    assert(nativeInvoke8()==36);
    std::puts("native Noqeri SysV 7/8 argument ABI: PASS");
}
