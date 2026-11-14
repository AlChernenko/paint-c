#include "checkerboard.h"

static GdkTexture *g_checker = NULL;

GdkTexture *
checkerboard_texture (void)
{
    if (g_checker) return g_checker;
    enum { CELL = 8, TILE = CELL * 2 };
    uint8_t pixels[TILE * TILE * 4];
    for (int y = 0; y < TILE; y++)
        for (int x = 0; x < TILE; x++) {
            int cell = ((x / CELL) + (y / CELL)) & 1;
            uint8_t v = cell ? 0xCC : 0xFF;
            uint8_t *p = &pixels[(y * TILE + x) * 4];
            p[0] = v; p[1] = v; p[2] = v; p[3] = 0xFF;
        }
    GBytes *bytes = g_bytes_new (pixels, sizeof pixels);
    g_checker = gdk_memory_texture_new (TILE, TILE,
        GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, TILE * 4);
    g_bytes_unref (bytes);
    return g_checker;
}
