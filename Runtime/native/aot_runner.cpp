#include "abi.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#if !defined(__linux__) || !(defined(__x86_64__) || defined(_M_X64))
#error "The qualified Noqeri AOT runner currently supports Linux x86-64 only."
#endif

extern "C" int noqeri_entry(const noqeri_abi* abi);

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
    return noqeri_entry(&abi);
}
