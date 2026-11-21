/* effects.c — see effects.h. Pure C, selection-aware, one undo step each. */

#include "effects.h"
#include "layer.h"
#include "pixel_buffer.h"

#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *
pnc_effect_name (PncEffect e)
{
    switch (e) {
    case EFFECT_GRAYSCALE:   return "Grayscale";
    case EFFECT_INVERT:      return "Invert";
    case EFFECT_SEPIA:       return "Sepia";
    case EFFECT_BRIGHTNESS:  return "Brightness";
    case EFFECT_CONTRAST:    return "Contrast";
    case EFFECT_GAUSSIAN_BLUR: return "Blur";
    case EFFECT_NOISE:       return "Add Noise";
    }
    return "Effect";
}

/* Effective work area: selection rect intersected with the document. */
static void
effect_bounds (Document *doc, int *x0, int *y0, int *x1, int *y1)
{
    int w = document_width (doc), h = document_height (doc);
    *x0 = 0; *y0 = 0; *x1 = w; *y1 = h;
    if (document_has_selection (doc)) {
        SelectionRect r = document_selection (doc);
        *x0 = CLAMP (r.x, 0, w); *y0 = CLAMP (r.y, 0, h);
        *x1 = CLAMP (r.x + r.w, 0, w); *y1 = CLAMP (r.y + r.h, 0, h);
    }
}

/* Separable box blur over a sub-rect of an independent source buffer. */
static void
box_blur_rect (const Pixel *src, Pixel *dst, int w, int h,
               int x0, int y0, int x1, int y1, int radius)
{
    if (radius < 1) radius = 1;
    uint16_t *tmp = g_new0 (uint16_t, (gsize)w * h * 4);

    /* horizontal pass src -> tmp */
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            unsigned sum[4] = {0, 0, 0, 0};
            int n = 0;
            for (int k = -radius; k <= radius; k++) {
                int sx = CLAMP (x + k, x0, x1 - 1);
                const Pixel *p = &src[(gsize)y * w + sx];
                sum[0] += p->b; sum[1] += p->g; sum[2] += p->r; sum[3] += p->a;
                n++;
            }
            uint16_t *t = &tmp[((gsize)y * w + x) * 4];
            t[0] = sum[0] / n; t[1] = sum[1] / n; t[2] = sum[2] / n; t[3] = sum[3] / n;
        }
    }
    /* vertical pass tmp -> dst */
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            unsigned sum[4] = {0, 0, 0, 0};
            int n = 0;
            for (int k = -radius; k <= radius; k++) {
                int sy = CLAMP (y + k, y0, y1 - 1);
                const uint16_t *t = &tmp[((gsize)sy * w + x) * 4];
                for (int c = 0; c < 4; c++) sum[c] += t[c];
                n++;
            }
            Pixel *d = &dst[(gsize)y * w + x];
            d->b = sum[0] / n; d->g = sum[1] / n; d->r = sum[2] / n; d->a = sum[3] / n;
        }
    }
    g_free (tmp);
}

gboolean
pnc_effect_apply (Document *doc, PncEffect effect, float strength, float factor)
{
    g_return_val_if_fail (doc != NULL, FALSE);

    Layer *l = document_layer_at (doc, document_active_layer (doc));
    if (!l || !l->buffer) return FALSE;

    int x0, y0, x1, y1;
    effect_bounds (doc, &x0, &y0, &x1, &y1);
    if (x1 <= x0 || y1 <= y0) return FALSE;

    const int w = l->buffer->width, h = l->buffer->height;
    Pixel *px = l->buffer->pixels;
    strength = CLAMP (strength, 0.0f, 1.0f);
    factor   = CLAMP (factor, 0.1f, 4.0f);

    document_begin_stroke (doc, pnc_effect_name (effect));

    switch (effect) {
    case EFFECT_GRAYSCALE:
    case EFFECT_SEPIA:
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                Pixel *p = &px[(gsize)y * w + x];
                float lum = 0.299f * p->r + 0.587f * p->g + 0.114f * p->b;
                float nb, ng, nr;
                if (effect == EFFECT_GRAYSCALE) { nb = ng = nr = lum; }
                else { /* sepia */
                    nr = MIN (255.f, lum * 1.2f + 20.f);
                    ng = MIN (255.f, lum * 1.0f + 8.f);
                    nb = lum * 0.82f;
                }
                p->r = (uint8_t)(p->r + (nr - p->r) * strength + 0.5f);
                p->g = (uint8_t)(p->g + (ng - p->g) * strength + 0.5f);
                p->b = (uint8_t)(p->b + (nb - p->b) * strength + 0.5f);
            }
        break;

    case EFFECT_INVERT:
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                Pixel *p = &px[(gsize)y * w + x];
                p->r = (uint8_t)(p->r + ((255 - p->r) - p->r) * strength + 0.5f);
                p->g = (uint8_t)(p->g + ((255 - p->g) - p->g) * strength + 0.5f);
                p->b = (uint8_t)(p->b + ((255 - p->b) - p->b) * strength + 0.5f);
            }
        break;

    case EFFECT_BRIGHTNESS:
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                Pixel *p = &px[(gsize)y * w + x];
                p->r = (uint8_t)CLAMP ((int)(p->r * factor + 0.5f), 0, 255);
                p->g = (uint8_t)CLAMP ((int)(p->g * factor + 0.5f), 0, 255);
                p->b = (uint8_t)CLAMP ((int)(p->b * factor + 0.5f), 0, 255);
            }
        break;

    case EFFECT_CONTRAST:
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                Pixel *p = &px[(gsize)y * w + x];
                p->r = (uint8_t)CLAMP ((int)((p->r - 128) * factor + 128 + 0.5f), 0, 255);
                p->g = (uint8_t)CLAMP ((int)((p->g - 128) * factor + 128 + 0.5f), 0, 255);
                p->b = (uint8_t)CLAMP ((int)((p->b - 128) * factor + 128 + 0.5f), 0, 255);
            }
        break;

    case EFFECT_GAUSSIAN_BLUR: {
        Pixel *src = g_new (Pixel, (gsize)w * h);
        memcpy (src, px, (gsize)w * h * sizeof (Pixel));
        int radius = MAX (1, (int)(strength * 12.0f));
        /* three box passes approximate a gaussian */
        for (int pass = 0; pass < 3; pass++) {
            box_blur_rect (src, px, w, h, x0, y0, x1, y1, radius);
            memcpy (src, px, (gsize)w * h * sizeof (Pixel));
        }
        g_free (src);
        break;
    }

    case EFFECT_NOISE: {
        GMersenneTwister *mt = g_rand_new_with_seed (g_get_real_time ());
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                Pixel *p = &px[(gsize)y * w + x];
                p->r = (uint8_t)CLAMP ((int)(p->r + (g_random_double_range (mt, -1, 1) * 128.0 * strength)), 0, 255);
                p->g = (uint8_t)CLAMP ((int)(p->g + (g_random_double_range (mt, -1, 1) * 128.0 * strength)), 0, 255);
                p->b = (uint8_t)CLAMP ((int)(p->b + (g_random_double_range (mt, -1, 1) * 128.0 * strength)), 0, 255);
            }
        g_rand_free (mt);
        break;
    }

    case EFFECT_COUNT:
        break;
    }

    document_end_stroke (doc);
    document_invalidate (doc);
    return TRUE;
}
