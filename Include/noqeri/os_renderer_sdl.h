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
/* Scaling profiles:
 * 0: crisp nearest-neighbor, integer upscaling whenever feasible (default).
 * 1: aspect-preserving linear filtering for smooth photographic scenes.
 * 2: aspect-preserving nearest-neighbor fractional fit.
 * The profile takes effect on an existing SDL texture and on future startup.
 * The SDL bridge is single-threaded; call control APIs on the render thread.
 */
int nq_os_renderer_set_scale_mode(int mode);
typedef struct nq_os_renderer_viewport {
    int x, y, width, height;
} nq_os_renderer_viewport;
/* Pure, bounded viewport calculator for presentation and headless tests.
 * Source size is 1..4096, output target is 1..16384 per dimension.
 */
int nq_os_renderer_fit_viewport(uint32_t source_width, uint32_t source_height,
                               uint32_t output_width, uint32_t output_height,
                               int mode, nq_os_renderer_viewport *result);
int nq_os_renderer_pump(uint32_t duration_ms);
int nq_os_renderer_close(void);
#ifdef __cplusplus
}
#endif
#endif
