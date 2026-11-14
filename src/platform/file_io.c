#include "file_io.h"
#include <gdk-pixbuf/gdk-pixbuf.h>

gboolean
file_io_save_png (Document *doc, const char *path, GError **error)
{
    int w = document_width (doc);
    int h = document_height (doc);
    const Pixel *src = (const Pixel *)document_composite (doc);

    GdkPixbuf *pb = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, w, h);
    if (!pb) {
        g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
                             "Could not allocate GdkPixbuf");
        return FALSE;
    }

    guchar *dst = gdk_pixbuf_get_pixels (pb);
    int stride  = gdk_pixbuf_get_rowstride (pb);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const Pixel *p = &src[(gsize)y * w + x];
            guchar *o = dst + y * stride + x * 4;
            o[0] = p->r; o[1] = p->g; o[2] = p->b; o[3] = p->a;
        }
    }

    gboolean ok = gdk_pixbuf_save (pb, path, "png", error, NULL);
    g_object_unref (pb);
    return ok;
}

Document *
file_io_open_png (const char *path, GError **error)
{
    GdkPixbuf *pb = gdk_pixbuf_new_from_file (path, error);
    if (!pb) return NULL;

    int w = gdk_pixbuf_get_width  (pb);
    int h = gdk_pixbuf_get_height (pb);
    gboolean has_alpha = gdk_pixbuf_get_has_alpha (pb);
    guchar *src = gdk_pixbuf_get_pixels (pb);
    int stride  = gdk_pixbuf_get_rowstride (pb);

    Document *doc = document_new (w, h);
    Layer *l = document_add_layer (doc, "Background");

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const guchar *s = src + y * stride + x * 4;
            Pixel *p = &l->buffer->pixels[(gsize)y * w + x];
            p->r = s[0]; p->g = s[1]; p->b = s[2];
            p->a = has_alpha ? s[3] : 255;
        }
    }

    g_object_unref (pb);
    document_invalidate (doc);
    document_mark_clean (doc);
    return doc;
}