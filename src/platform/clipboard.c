#include "clipboard.h"
#include "../util/log.h"
#include <gtk/gtk.h>
#include <string.h>

static PixelBuffer *g_internal = NULL;

void
clipboard_set (const PixelBuffer *buf)
{
    pixel_buffer_free (g_internal);
    g_internal = NULL;
    if (!buf) return;
    g_internal = pixel_buffer_new (buf->width, buf->height);
    memcpy (g_internal->pixels, buf->pixels,
            (gsize)buf->width * buf->height * sizeof (Pixel));
}

bool clipboard_has_internal (void) { return g_internal != NULL; }

PixelBuffer *
clipboard_get_internal (void)
{
    if (!g_internal) return NULL;
    PixelBuffer *pb = pixel_buffer_new (g_internal->width, g_internal->height);
    memcpy (pb->pixels, g_internal->pixels,
            (gsize)g_internal->width * g_internal->height * sizeof (Pixel));
    return pb;
}

void
clipboard_set_system (const PixelBuffer *buf)
{
    if (!buf) return;
    GdkDisplay *disp = gdk_display_get_default ();
    if (!disp) return;

    /* Straight → premultiplied for GdkTexture */
    gsize n = (gsize)buf->width * buf->height;
    Pixel *premul = g_new (Pixel, n);
    for (gsize i = 0; i < n; i++) {
        Pixel p = buf->pixels[i];
        premul[i].r = (uint8_t)((p.r * p.a + 127) / 255);
        premul[i].g = (uint8_t)((p.g * p.a + 127) / 255);
        premul[i].b = (uint8_t)((p.b * p.a + 127) / 255);
        premul[i].a = p.a;
    }
    GBytes *bytes = g_bytes_new_take (premul, n * sizeof (Pixel));
    GdkTexture *tex = gdk_memory_texture_new (buf->width, buf->height,
        GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, buf->width * 4);
    g_bytes_unref (bytes);

    GdkClipboard *cb = gdk_display_get_clipboard (disp);
    gdk_clipboard_set_texture (cb, tex);
    g_object_unref (tex);
    LOG_DEBUG ("copied %dx%d to system clipboard", buf->width, buf->height);
}

typedef struct {
    ClipboardPasteFn cb;
    void *user_data;
} PasteCtx;

static void
on_system_paste_done (GObject *src, GAsyncResult *res, gpointer ud)
{
    PasteCtx *ctx = ud;
    GError *err = NULL;
    GdkTexture *tex = gdk_clipboard_read_texture_finish (
        GDK_CLIPBOARD (src), res, &err);
    if (!tex) {
        LOG_WARN ("system paste failed: %s", err ? err->message : "no texture");
        if (err) g_error_free (err);
        ctx->cb (NULL, ctx->user_data);
        g_free (ctx);
        return;
    }

    int w = gdk_texture_get_width (tex);
    int h = gdk_texture_get_height (tex);
    gsize stride = (gsize)w * 4;
    guchar *data = g_malloc (stride * h);
    gdk_texture_download (tex, data, stride);
    g_object_unref (tex);

    PixelBuffer *buf = pixel_buffer_new (w, h);
    for (gsize i = 0; i < (gsize)w * h; i++) {
        uint8_t b = data[i*4+0], g = data[i*4+1];
        uint8_t r = data[i*4+2], a = data[i*4+3];
        Pixel p;
        if (a == 0)        { p.r = p.g = p.b = p.a = 0; }
        else if (a == 255) { p.r = r; p.g = g; p.b = b; p.a = 255; }
        else {
            /* Un-premultiply */
            p.r = (uint8_t)MIN (255, (r * 255 + a / 2) / a);
            p.g = (uint8_t)MIN (255, (g * 255 + a / 2) / a);
            p.b = (uint8_t)MIN (255, (b * 255 + a / 2) / a);
            p.a = a;
        }
        buf->pixels[i] = p;
    }
    g_free (data);

    ctx->cb (buf, ctx->user_data);
    pixel_buffer_free (buf);
    g_free (ctx);
    LOG_DEBUG ("pasted %dx%d from system clipboard", w, h);
}

void
clipboard_get_system_async (ClipboardPasteFn cb, void *user_data)
{
    GdkDisplay *disp = gdk_display_get_default ();
    if (!disp) { cb (NULL, user_data); return; }
    GdkClipboard *c = gdk_display_get_clipboard (disp);

    PasteCtx *ctx = g_new0 (PasteCtx, 1);
    ctx->cb = cb;
    ctx->user_data = user_data;
    gdk_clipboard_read_texture_async (c, NULL, on_system_paste_done, ctx);
}
