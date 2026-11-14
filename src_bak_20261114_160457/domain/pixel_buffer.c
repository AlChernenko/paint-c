#include "pixel_buffer.h"
#include <glib.h>       /* g_new0 / g_free / g_return_val_if_fail */
#include <string.h>

PixelBuffer *
pixel_buffer_new (int width, int height)
{
    g_return_val_if_fail (width > 0 && height > 0, NULL);

    PixelBuffer *pb = g_new0 (PixelBuffer, 1);
    pb->width  = width;
    pb->height = height;
    pb->pixels = g_new0 (Pixel, (gsize)width * (gsize)height);
    /* calloc -> every pixel is {0,0,0,0} i.e. fully transparent. */

    return pb;
}

void
pixel_buffer_free (PixelBuffer *pb)
{
    if (!pb) return;
    g_free (pb->pixels);
    g_free (pb);
}

void
pixel_buffer_fill_rect (PixelBuffer *pb,
                        int x, int y, int w, int h,
                        Pixel color)
{
    g_return_if_fail (pb != NULL);

    /* Clip to buffer bounds — no assertions, just skip out-of-range. */
    int x0 = MAX (x, 0);
    int y0 = MAX (y, 0);
    int x1 = MIN (x + w, pb->width);
    int y1 = MIN (y + h, pb->height);

    for (int yy = y0; yy < y1; yy++) {
        Pixel *row = &pb->pixels[(gsize)yy * pb->width];
        for (int xx = x0; xx < x1; xx++) {
            row[xx] = color;
        }
    }
}