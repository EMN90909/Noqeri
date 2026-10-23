#ifndef NOQERI_OS_RENDERER_SDL_H
#define NOQERI_OS_RENDERER_SDL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Optional desktop SDL2 presentation ABI. Raster operations are implemented
 * by Noqeri. Pixel format: host-native 32-bit ARGB8888. The buffer is borrowed
 * for the duration of the call; pitch is tightly packed, width*4 bytes.
 * Failed or unavailable display backends return negative errno-style codes.
 */
int nq_os_renderer_present(const uint32_t *argb, size_t pixel_count,
                           uint32_t width, uint32_t height);
int nq_os_renderer_pump(uint32_t duration_ms);
int nq_os_renderer_close(void);
#ifdef __cplusplus
}
#endif
#endif
