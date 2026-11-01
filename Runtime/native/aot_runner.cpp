#include "abi.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>

#if !defined(__linux__) || !(defined(__x86_64__) || defined(_M_X64))
#error "The qualified Noqeri AOT runner currently supports Linux x86-64 only."
#endif

// Hosted string owners survive until process exit. std::deque preserves
// addresses even for small-string-optimized entries. Enforce caps.
namespace {
thread_local std::deque<std::string> interpolated;
thread_local std::size_t interpolationBytes=0;
const char* saveInterpolation(std::string value){
    if(value.size()>1048576||interpolated.size()>=65536||
       interpolationBytes>16777216-value.size())std::abort();
    interpolationBytes+=value.size();
    interpolated.push_back(std::move(value));
    return interpolated.back().c_str();
}
}
extern "C" const char* nq_internal_interp_join(const char* left,const char* right){
    if(!left||!right)std::abort();
    const std::string a(left),b(right);
    if(a.size()>1048576||b.size()>1048576-a.size())std::abort();
    return saveInterpolation(a+b);
}
extern "C" const char* nq_internal_interp_i64(std::int64_t value){
    return saveInterpolation(std::to_string(value));
}
extern "C" const char* nq_internal_interp_u64(std::uint64_t value){
    return saveInterpolation(std::to_string(value));
}
extern "C" const char* nq_internal_interp_bool(std::int64_t value){
    return saveInterpolation(value?"true":"false");
}
// NIR passes scalar f64 payload bits through the GPR call ABI on x86-64.
extern "C" const char* nq_internal_interp_float(std::uint64_t rawBits){
    double value=0.0;
    std::memcpy(&value,&rawBits,sizeof(value));
    return saveInterpolation(std::to_string(value));
}
extern "C" int noqeri_entry(const noqeri_abi* abi);
extern "C" int noqeri_entry_abi_probe(const noqeri_abi* abi);

asm(R"ASM(
.text
.global noqeri_entry_abi_probe
.type noqeri_entry_abi_probe,@function
noqeri_entry_abi_probe:
    push %rbx
    movabs $0x1122334455667788, %rbx
    call noqeri_entry
    movabs $0x1122334455667788, %r11
    cmp %r11, %rbx
    jne .L_noqeri_abi_probe_fail
    pop %rbx
    ret
.L_noqeri_abi_probe_fail:
    mov $125, %eax
    pop %rbx
    ret
.size noqeri_entry_abi_probe, .-noqeri_entry_abi_probe
)ASM");

namespace {

int32_t writeOutput(void*, const char* data, size_t size) {
    if (size == 0) return 0;
    if (!data) return 1;
    return std::fwrite(data, 1, size, stdout) == size ? 0 : 1;
}

int64_t clockMillis(void*) {
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

noqeri_abi_string platformName(void*) {
    static constexpr char kPlatform[] = "linux";
    return {kPlatform, sizeof(kPlatform) - 1};
}

}  // namespace

int main() {
    const noqeri_abi abi{
        NOQERI_ABI_VERSION,
        sizeof(noqeri_abi),
        nullptr,
        writeOutput,
        clockMillis,
        platformName,
        0,
        nullptr,
    };
    return noqeri_entry_abi_probe(&abi);
}
