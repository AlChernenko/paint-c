/* pixel_buffer.h — canonical pixel storage.
 *
 * LAYER: L2 (domain). Do NOT include GTK in this file — see architecture §2.
 *
 * Pixel layout: BGRA8, straight alpha, sRGB, row-major, top-left origin.
 * This matches GDK_MEMORY_B8G8R8A8_PREMULTIPLIED *only when alpha==255*.
 * For partially-transparent composites the canvas does a premultiply pass
 * before uploading — TODO in canvas.c.
 *
 * EXTENSION POINTS:
 *   - Replace `Pixel *pixels` with a refcounted struct if layers ever need
 *     to share pixel data (e.g. clone layers). See ADR-05.
 *   - Add `void *mmap_base` + a factory function for very large buffers
 *     (DevDoc §9 checklist).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Memory layout matches cairo ARGB32 and GDK B8G8R8A8.
 * Fields are ordered b, g, r, a so a `Pixel` cast to `uint32_t*` on
 * little-endian hosts gives 0xAARRGGBB — do NOT reorder. */
typedef struct {
    uint8_t b, g, r, a;
} Pixel;

/* A flat, tightly packed W×H pixel grid.
 * Does NOT carry layer metadata — that lives in Layer. This separation
 * lets us swap PixelBuffer for an mmap-backed implementation later. */
typedef struct {
    int     width;
    int     height;
    Pixel  *pixels;    /* width * height, calloc'd (transparent) */
} PixelBuffer;

/* Allocates width*height Pixel entries, zero-initialized.
 * Returns NULL if width<=0 or height<=0 or allocation fails.
 * FREE WITH pixel_buffer_free(). Do not `free(pb->pixels)` directly. */
PixelBuffer *pixel_buffer_new  (int width, int height);
void         pixel_buffer_free (PixelBuffer *pb);

/* Convenience: blit a solid rectangle. Clipped to buffer bounds.
 * Used by the window to paint the hardcoded white background in M1.
 * TODO: honour a SelectionMask once one exists (M6). */
void         pixel_buffer_fill_rect (PixelBuffer *pb,
                                     int x, int y, int w, int h,
                                     Pixel color);