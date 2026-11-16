#include "compositor.h"
#include <glib.h>
#include <string.h>
#include <math.h>

/* Straight-alpha source-over, plus multiply/screen.
 *
 * Math reference: Porter–Duff "over" with a blend function applied first.
 *   Ca' = (1 - aa) * Cb + aa * [(1-ab) * Cs + ab * B(Cb, Cs)]
 * where B() is the blend function. We implement normal (B = Cs),
 * multiply, and screen.
 */
void
compositor_blend_pixel (Pixel *dst, const Pixel *src,
                        float opacity, BlendMode mode)
{
    float sa = (src->a / 255.0f) * opacity;
    if (sa <= 0.0f) return;

    float sr = src->r / 255.0f;
    float sg = src->g / 255.0f;
    float sb = src->b / 255.0f;

    float dr = dst->r / 255.0f;
    float dg = dst->g / 255.0f;
    float db = dst->b / 255.0f;

    /* Blend function B(Cb, Cs) — result goes into (sr, sg, sb). */
    if (mode == BLEND_MULTIPLY) {
        sr = sr * dr; sg = sg * dg; sb = sb * db;
    } else if (mode == BLEND_SCREEN) {
        sr = 1.0f - (1.0f - sr) * (1.0f - dr);
        sg = 1.0f - (1.0f - sg) * (1.0f - dg);
        sb = 1.0f - (1.0f - sb) * (1.0f - db);
    } else if (mode == BLEND_OVERLAY) {
        sr = (dr <= 0.5f) ? (2.0f * dr * sr) : (1.0f - 2.0f * (1.0f - dr) * (1.0f - sr));
        sg = (dg <= 0.5f) ? (2.0f * dg * sg) : (1.0f - 2.0f * (1.0f - dg) * (1.0f - sg));
        sb = (db <= 0.5f) ? (2.0f * db * sb) : (1.0f - 2.0f * (1.0f - db) * (1.0f - sb));
    } else if (mode == BLEND_DIFFERENCE) {
        sr = fabsf(dr - sr); sg = fabsf(dg - sg); sb = fabsf(db - sb);
    } else if (mode == BLEND_ADDITION) {
        sr = MIN(1.0f, dr + sr); sg = MIN(1.0f, dg + sg); sb = MIN(1.0f, db + sb);
    }
    /* BLEND_NORMAL: sr/sg/sb unchanged. */

    float da  = dst->a / 255.0f;
    float out = sa + da * (1.0f - sa);
    if (out <= 0.0f) {
        dst->r = dst->g = dst->b = dst->a = 0;
        return;
    }

    /* NOTE: this division-by-out is what makes us "straight alpha". The
     * mathematically equivalent premultiplied form is faster and needs no
     * division — see canvas.c's TODO for a future premultiplied pipeline. */
    dst->r = (uint8_t)(255.0f * ((sr * sa) + dr * da * (1.0f - sa)) / out + 0.5f);
    dst->g = (uint8_t)(255.0f * ((sg * sa) + dg * da * (1.0f - sa)) / out + 0.5f);
    dst->b = (uint8_t)(255.0f * ((sb * sa) + db * da * (1.0f - sa)) / out + 0.5f);
    dst->a = (uint8_t)(255.0f * out + 0.5f);
}

void
compositor_composite (Pixel *dst,
                      Layer * const *layers, int count,
                      int width, int height)
{
    memset (dst, 0, (gsize)width * (gsize)height * sizeof (Pixel));

    for (int i = 0; i < count; i++) {
        const Layer *l = layers[i];
        if (!l || !l->visible || l->opacity <= 0.0f || !l->buffer)
            continue;

        const Pixel *src = l->buffer->pixels;
        for (gsize p = 0; p < (gsize)width * (gsize)height; p++) {
            compositor_blend_pixel (&dst[p], &src[p],
                                    l->opacity, l->blend_mode);
        }
    }
}