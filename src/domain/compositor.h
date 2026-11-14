/* compositor.h — pure pixel-blending math. Stateless.
 * LAYER: L2 (domain). No GTK. No DocumentObserver here — that lives in
 * document.h. If you see a DocumentObserver typedef in this file, it's
 * a stale copy — delete it.
 */

#pragma once

#include "pixel_buffer.h"
#include "layer.h"

void compositor_blend_pixel (Pixel *dst, const Pixel *src,
                             float opacity, BlendMode mode);

void compositor_composite (Pixel *dst,
                           Layer * const *layers, int count,
                           int width, int height);
