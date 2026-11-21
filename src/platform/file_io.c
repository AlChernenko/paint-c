/* file_io.c — PNG/JPEG/WebP/PDF export + raster/.pncimg import.
 *
 * LAYER: L1 (platform).
 *
 * Pixel layout used by the document composite: BGRA8, straight
 * (unpremultiplied) alpha — see domain/pixel_buffer.h. GdkPixbuf stores
 * RGBA8 straight alpha, so we swizzle B/R when copying and must NOT
 * premultiply here; gdk_pixbuf_save() handles encoding itself.
 *
 * .pncimg container (version 1):
 *   "PNCI" | u32le version | u32le w | u32le h | u32le layer_count
 *   then per layer (bottom-to-top):
 *     u32le name_len | name bytes | f32 opacity (IEEE754 LE) |
 *     u32le blend_mode | u8 visible | raw w*h*4 BGRA pixels
 *   All integers little-endian; no compression (MVP — TODO: zlib).
 */
#include "file_io.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <cairo/cairo.h>
#include <string.h>
#include <math.h>

#define PNC_MAGIC   "PNCI"
#define PNC_VERSION 1u

/* ── Small helpers ─────────────────────────────────────────────────── */

static void put_u32 (GByteArray *a, guint32 v)
{
    guint8 b[4] = { v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF };
    g_byte_array_append (a, b, 4);
}

static void put_f32 (GByteArray *a, float f)
{
    guint32 v;
    memcpy (&v, &f, 4);
    put_u32 (a, v);
}

typedef struct { const guint8 *p; gsize n, pos; gboolean ok; } Rdr;

static guint32 rd_u32 (Rdr *r)
{
    if (!r->ok || r->pos + 4 > r->n) { r->ok = FALSE; return 0; }
    guint32 v = (guint32)r->p[r->pos] | ((guint32)r->p[r->pos+1] << 8) |
                ((guint32)r->p[r->pos+2] << 16) | ((guint32)r->p[r->pos+3] << 24);
    r->pos += 4;
    return v;
}

static float rd_f32 (Rdr *r)
{
    guint32 v = rd_u32 (r);
    float f;
    memcpy (&f, &v, 4);
    return f;
}

static guint8 rd_u8 (Rdr *r)
{
    if (!r->ok || r->pos + 1 > r->n) { r->ok = FALSE; return 0; }
    return r->p[r->pos++];
}

/* Force a fresh recomposite before capturing. With multiple layers a stale
 * cached composite is the root cause of the known "corrupted PNG when
 * saving with multiple layers" bug. */
static gboolean
capture_composite (Document *doc, int *w, int *h, GError **error)
{
    document_invalidate (doc);
    *w = document_width (doc);
    *h = document_height (doc);
    const Pixel *src = (const Pixel *)document_composite (doc);
    if (*w <= 0 || *h <= 0 || !src) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                     "Cannot save empty image (%dx%d)", *w, *h);
        return FALSE;
    }
    return TRUE;
}

/* Composite BGRA8 -> newly allocated GdkPixbuf (RGBA8, straight alpha). */
static GdkPixbuf *
composite_to_pixbuf (Document *doc)
{
    int w = document_width (doc), h = document_height (doc);
    const Pixel *src = (const Pixel *)document_composite (doc);
    GdkPixbuf *pb = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, w, h);
    guchar *dst = gdk_pixbuf_get_pixels (pb);
    int stride = gdk_pixbuf_get_rowstride (pb);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Pixel p = src[(gsize)y * w + x];      /* BGRA, straight alpha */
            guchar *o = dst + (gsize)y * stride + (gsize)x * 4;
            o[0] = p.r; o[1] = p.g; o[2] = p.b; o[3] = p.a;
        }
    }
    return pb;
}

/* ── Format detection ──────────────────────────────────────────────── */

gboolean
file_io_format_from_path (const char *path, PncFormat *out)
{
    if (!path || !out) return FALSE;
    char *base = g_path_get_basename (path);
    char *lower = g_ascii_strdown (base, -1);
    g_free (base);
    gboolean ok = TRUE;
    if      (g_str_has_suffix (lower, ".png"))               *out = PNC_FORMAT_PNG;
    else if (g_str_has_suffix (lower, ".jpg") ||
             g_str_has_suffix (lower, ".jpeg"))              *out = PNC_FORMAT_JPEG;
    else if (g_str_has_suffix (lower, ".webp"))              *out = PNC_FORMAT_WEBP;
    else if (g_str_has_suffix (lower, ".pdf"))               *out = PNC_FORMAT_PDF;
    else if (g_str_has_suffix (lower, ".pncimg"))            *out = PNC_FORMAT_PNCIMG;
    else                                                      ok = FALSE;
    g_free (lower);
    return ok;
}

/* ── PDF export (cairo) ────────────────────────────────────────────── */

static gboolean
save_pdf (Document *doc, const char *path, GError **error)
{
    int w, h;
    if (!capture_composite (doc, &w, &h, error)) return FALSE;

    cairo_surface_t *surf = cairo_pdf_surface_create (path, w, h);
    if (cairo_surface_status (surf) != CAIRO_STATUS_SUCCESS) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_IO,
                     "Could not create PDF surface: %s",
                     cairo_status_to_string (cairo_surface_status (surf)));
        cairo_surface_destroy (surf);
        return FALSE;
    }
    cairo_t *cr = cairo_create (surf);

    /* Checkerboard-free white page background so transparent areas print. */
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_paint (cr);

    /* One page per visible layer (bottom to top), then the flattened
     * composite as the final page. */
    int n = document_layer_count (doc);
    for (int i = 0; i < n; i++) {
        Layer *l = document_layer_at (doc, i);
        if (!l || !l->visible || !l->buffer) continue;
        cairo_append_page (cr);
        cairo_set_source_rgb (cr, 1, 1, 1);
        cairo_paint (cr);
        cairo_surface_t *lsurf = cairo_image_surface_create_for_data (
            (guchar *)l->buffer->pixels, CAIRO_FORMAT_ARGB32, w, h, w * 4);
        cairo_set_source_surface (cr, lsurf, 0, 0);
        cairo_paint_with_alpha (cr, l->opacity);
        cairo_surface_destroy (lsurf);
    }

    /* Final flattened page. */
    cairo_append_page (cr);
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_paint (cr);
    cairo_surface_t *csurf = cairo_image_surface_create_for_data (
        (guchar *)document_composite (doc), CAIRO_FORMAT_ARGB32, w, h, w * 4);
    cairo_set_source_surface (cr, csurf, 0, 0);
    cairo_paint (cr);
    cairo_surface_destroy (csurf);

    cairo_destroy (cr);
    cairo_surface_flush (surf);
    gboolean ok = cairo_surface_status (surf) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy (surf);
    if (!ok)
        g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_IO,
                             "PDF write failed");
    return ok;
}

/* ── Save dispatch ─────────────────────────────────────────────────── */

gboolean
file_io_save (Document *doc, const char *path, PncFormat format, GError **error)
{
    if (!doc || !path) {
        g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                             "No document or path to save");
        return FALSE;
    }

    if (format == PNC_FORMAT_PDF)
        return save_pdf (doc, path, error);

    if (format == PNC_FORMAT_PNCIMG) {
        int w, h;
        if (!capture_composite (doc, &w, &h, error)) return FALSE;

        GByteArray *a = g_byte_array_new ();
        g_byte_array_append (a, (const guint8 *)PNC_MAGIC, 4);
        put_u32 (a, PNC_VERSION);
        put_u32 (a, (guint32)w);
        put_u32 (a, (guint32)h);
        int n = document_layer_count (doc);
        put_u32 (a, (guint32)n);

        for (int i = 0; i < n; i++) {           /* bottom-to-top order */
            Layer *l = document_layer_at (doc, i);
            if (!l) continue;
            const char *name = l->name ? l->name : "Layer";
            gsize name_len = strnlen (name, 255);
            put_u32 (a, (guint32)name_len);
            g_byte_array_append (a, (const guint8 *)name, (guint)name_len);
            put_f32 (a, l->opacity);
            put_u32 (a, (guint32)l->blend_mode);
            g_byte_array_append (a, (const guint8[]){ l->visible ? 1 : 0 }, 1);
            g_byte_array_append (a, (const guint8 *)l->buffer->pixels,
                                 (gsize)w * h * sizeof (Pixel));
        }

        gboolean ok = g_file_set_contents (path, (const char *)a->data,
                                           (gssize)a->len, error);
        g_byte_array_unref (a);
        return ok;
    }

    /* Raster formats via GdkPixbuf. */
    int w, h;
    if (!capture_composite (doc, &w, &h, error)) return FALSE;

    GdkPixbuf *pb = composite_to_pixbuf (doc);
    const char *type = (format == PNC_FORMAT_JPEG) ? "jpeg" :
                       (format == PNC_FORMAT_WEBP) ? "webp" : "png";

    gboolean ok;
    if (format == PNC_FORMAT_JPEG) {
        /* JPEG has no alpha channel: flatten onto white first. */
        GdkPixbuf *flat = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, w, h);
        gdk_pixbuf_fill (flat, 0xFFFFFFFF);
        gdk_pixbuf_composite (pb, flat, 0, 0, w, h, 0, 0, 1.0, 1.0,
                              GDK_INTERP_NEAREST, 255);
        ok = gdk_pixbuf_save (flat, path, type, error, "quality", "90", NULL);
        g_object_unref (flat);
    } else {
        ok = gdk_pixbuf_save (pb, path, type, error, NULL);
    }
    g_object_unref (pb);
    return ok;
}

gboolean
file_io_save_png (Document *doc, const char *path, GError **error)
{
    return file_io_save (doc, path, PNC_FORMAT_PNG, error);
}

/* ── Open ──────────────────────────────────────────────────────────── */

static Document *
open_pncimg (const char *path, GError **error)
{
    char *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents (path, &contents, &len, error)) return NULL;

    Rdr r = { (const guint8 *)contents, len, 0, TRUE };
    if (r.n < 20 || memcmp (r.p, PNC_MAGIC, 4) != 0) {
        g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                             "Not a paint-c document (.pncimg)");
        g_free (contents);
        return NULL;
    }
    r.pos += 4;
    guint32 ver = rd_u32 (&r);
    if (ver != PNC_VERSION) {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                     "Unsupported .pncimg version %u", ver);
        g_free (contents);
        return NULL;
    }
    guint32 w = rd_u32 (&r), h = rd_u32 (&r), n = rd_u32 (&r);
    if (!r.ok || w == 0 || h == 0 || w > 65535 || h > 65535 || n > 1024) {
        g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                             "Corrupt .pncimg header");
        g_free (contents);
        return NULL;
    }

    Document *doc = document_new ((int)w, (int)h);
    gsize pix_bytes = (gsize)w * h * sizeof (Pixel);

    for (guint32 i = 0; i < n; i++) {
        guint32 name_len = rd_u32 (&r);
        if (!r.ok || name_len > r.n - r.pos) break;
        char *name = g_strndup ((const char *)(r.p + r.pos), name_len);
        r.pos += name_len;
        float opacity = rd_f32 (&r);
        guint32 blend = rd_u32 (&r);
        guint8 visible = rd_u8 (&r);
        if (!r.ok || r.pos + pix_bytes > r.n) {
            g_free (name);
            break;
        }
        Layer *l = document_add_layer (doc, name);
        g_free (name);
        memcpy (l->buffer->pixels, r.p + r.pos, pix_bytes);
        r.pos += pix_bytes;
        l->opacity = CLAMP (opacity, 0.0f, 1.0f);
        l->blend_mode = (blend < BLEND_MODE_COUNT) ? (BlendMode)blend : BLEND_NORMAL;
        l->visible = visible != 0;
    }

    if (!r.ok || document_layer_count (doc) == 0) {
        g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                             "Truncated or corrupt .pncimg data");
        document_free (doc);
        g_free (contents);
        return NULL;
    }

    g_free (contents);
    document_invalidate (doc);
    document_mark_clean (doc);
    return doc;
}

static Document *
open_raster (const char *path, GError **error)
{
    GdkPixbuf *pb = gdk_pixbuf_new_from_file (path, error);
    if (!pb) return NULL;

    int w = gdk_pixbuf_get_width  (pb);
    int h = gdk_pixbuf_get_height (pb);
    gboolean has_alpha = gdk_pixbuf_get_has_alpha (pb);
    guchar *src = gdk_pixbuf_get_pixels (pb);
    int stride = gdk_pixbuf_get_rowstride (pb);

    Document *doc = document_new (w, h);
    Layer *l = document_add_layer (doc, "Background");

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const guchar *s = src + (gsize)y * stride + (gsize)x * (has_alpha ? 4 : 3);
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

Document *
file_io_open (const char *path, GError **error)
{
    if (!path) {
        g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                             "No path given");
        return NULL;
    }
    char *lower = g_ascii_strdown (path, -1);
    gboolean is_pnc = g_str_has_suffix (lower, ".pncimg");
    gboolean is_pdf = g_str_has_suffix (lower, ".pdf");
    g_free (lower);

    if (is_pdf) {
        g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_NOT_SUPPORTED,
                             "Opening PDF files is not supported "
                             "(paint-c can only export PDF)");
        return NULL;
    }
    if (is_pnc) return open_pncimg (path, error);
    return open_raster (path, error);
}

Document *
file_io_open_png (const char *path, GError **error)
{
    return file_io_open (path, error);
}
