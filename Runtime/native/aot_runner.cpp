#include "abi.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#if !defined(__linux__) || !(defined(__x86_64__) || defined(_M_X64))
#error "The qualified Noqeri AOT runner currently supports Linux x86-64 only."
#endif

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
