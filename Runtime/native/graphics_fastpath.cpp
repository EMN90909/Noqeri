// Portable C++ native framebuffer fast path with x86-64 SSE2 / AArch64 NEON.
// This is CPU SIMD, NOT GPU execution or volatile framebuffer MMIO.
#include "noqeri/graphics_fastpath.h"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
// Keep the binary baseline safe for older CPUs. x86-64 guarantees SSE2,
// while x86-32 uses it only when the compiler explicitly enables it.
// ARM NEON is used only when the toolchain exposes intrinsics for this target.
// Other CPU architectures use the same bit-exact scalar algorithm.
#if !defined(NOQERI_GRAPHICS_FORCE_SCALAR)
# if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#  include <emmintrin.h>
#  define NOQERI_GRAPHICS_SSE2 1
# elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM64)
#  include <arm_neon.h>
#  define NOQERI_GRAPHICS_NEON 1
# endif
#endif
namespace {
inline void fill_pixels(std::uint32_t* pixels,std::size_t n,std::uint32_t color) noexcept {
    std::size_t i=0;
#if defined(NOQERI_GRAPHICS_SSE2)
    // Unaligned stores work for caller-owned buffers and avoid alignment prologues.
    const __m128i packed=_mm_set1_epi32(static_cast<int>(color));
    for(; n-i>=16; i+=16) {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels+i),packed);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels+i+4),packed);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels+i+8),packed);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels+i+12),packed);
    }
    for(; n-i>=4; i+=4)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(pixels+i),packed);
#elif defined(NOQERI_GRAPHICS_NEON)
    const uint32x4_t packed=vdupq_n_u32(color);
    for(; n-i>=16; i+=16) {
        vst1q_u32(pixels+i,packed);
        vst1q_u32(pixels+i+4,packed);
        vst1q_u32(pixels+i+8,packed);
        vst1q_u32(pixels+i+12,packed);
    }
    for(; n-i>=4; i+=4)vst1q_u32(pixels+i,packed);
#endif
    for(;i<n;++i)pixels[i]=color;
}
}
extern "C" int nq_graphics_clear_argb32(std::uint32_t* pixels,std::size_t count,
                                          std::uint32_t color) {
    if(count==0)return 0;
    if(!pixels)return -EINVAL;
    // Framebuffer profile: maximum 4096x4096 pixels. Reject pathological
    // lengths before touching memory. Caller still owns the memory lifetime.
    if(count>4096u*4096u)return -ERANGE;
    fill_pixels(pixels,count,color);
    return 0;
}
extern "C" int nq_graphics_clear_rows_argb32(std::uint32_t* pixels,
                                              std::size_t capacity,
                                              std::size_t width,
                                              std::size_t height,
                                              std::size_t pitch,
                                              std::uint32_t color) {
    if(!pixels||width==0||height==0||width>4096||height>4096||
       pitch<width||pitch>65536||height>capacity/pitch)
        return -EINVAL;
    // Visible rows are clipped to width; never fill pitch padding.
    for(std::size_t y=0;y<height;++y)
        fill_pixels(pixels+y*pitch,width,color);
    return 0;
}

extern "C" const char* nq_graphics_clear_backend(void) {
#if defined(NOQERI_GRAPHICS_SSE2)
    return "sse2";
#elif defined(NOQERI_GRAPHICS_NEON)
    return "neon";
#else
    return "scalar";
#endif
}
extern "C" const char* nq_graphics_cpu_arch(void) {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__riscv) && (__riscv_xlen == 64)
    return "riscv64";
#elif defined(__riscv)
    return "riscv";
#elif defined(__powerpc64__) || defined(__ppc64__)
    return "ppc64";
#elif defined(__powerpc__) || defined(__ppc__)
    return "ppc";
#elif defined(__s390x__)
    return "s390x";
#elif defined(__mips64)
    return "mips64";
#else
    return "other";
#endif
}
extern "C" const char* nq_graphics_host_os(void) {
#if defined(__ANDROID__)
    return "android";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(__linux__)
    return "linux";
#else
    return "other";
#endif
}
