#ifndef NOQERI_GRAPHICS_FASTPATH_H
#define NOQERI_GRAPHICS_FASTPATH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Native, nonvolatile CPU framebuffer acceleration. All pointers are borrowed.
 * The owner must provide writable storage of at least pixel_count u32 values.
 * These functions MUST NOT be used on volatile/MMIO framebuffers. No JavaScript,
 * browser APIs or device drivers are involved. Negative errno-style results.
 */
int nq_graphics_clear_argb32(uint32_t *pixels, size_t pixel_count, uint32_t color);
/* Row pitch is in u32 pixels, not bytes. Only visible width is touched.
 * Pixel capacity includes padding and is checked before any writes.
 */
int nq_graphics_clear_rows_argb32(uint32_t *pixels, size_t pixel_capacity,
                                 size_t width, size_t height, size_t pitch,
                                 uint32_t color);
#ifdef __cplusplus
}
#endif
#endif
