#include "document.h"
#include "compositor.h"

#include <glib.h>
#include <string.h>

/* ── Observer list node ─────────────────────────────────────────────── */
struct _DocumentObserverNode {
    const DocumentObserver      *obs;
    void                        *user_data;
    struct _DocumentObserverNode *next;
};

struct Document {
    int     width, height;
    Layer **layers;
    int     layer_count, layer_capacity;
    int     active_layer;
    Pixel  *composite;

    struct _DocumentObserverNode *observers;   /* linked list */
    bool                          dirty;

    /* Rectangular selection in image coords; has_sel = FALSE → no clip. */
    bool  has_sel;
    int   sel_x, sel_y, sel_w, sel_h;
};

/* ── Notify helpers ─────────────────────────────────────────────────── */
static void
notify_composite_changed (Document *doc)
{
    for (struct _DocumentObserverNode *n = doc->observers; n; n = n->next)
        if (n->obs->on_composite_changed)
            n->obs->on_composite_changed (n->user_data);
}

static void
notify_layer_list_changed (Document *doc)
{
    for (struct _DocumentObserverNode *n = doc->observers; n; n = n->next)
        if (n->obs->on_layer_list_changed)
            n->obs->on_layer_list_changed (n->user_data);
}

static void
notify_active_layer_changed (Document *doc)
{
    for (struct _DocumentObserverNode *n = doc->observers; n; n = n->next)
        if (n->obs->on_active_layer_changed)
            n->obs->on_active_layer_changed (n->user_data);
}

static inline bool
pixel_in_selection (const Document *d, int x, int y)
{
    if (!d->has_sel) return true;
    return x >= d->sel_x && x < d->sel_x + d->sel_w &&
           y >= d->sel_y && y < d->sel_y + d->sel_h;
}

static void
notify_selection_changed (Document *doc)
{
    for (struct _DocumentObserverNode *n = doc->observers; n; n = n->next)
        if (n->obs->on_selection_changed)
            n->obs->on_selection_changed (n->user_data);
}

/* ── Recomposition + invalidation ───────────────────────────────────── */
static void
document_recomposite (Document *doc)
{
    compositor_composite (doc->composite,
                          doc->layers, doc->layer_count,
                          doc->width, doc->height);
}

static void
document_invalidate (Document *doc)
{
    document_recomposite (doc);
    notify_composite_changed (doc);
}

/* ── Lifecycle ──────────────────────────────────────────────────────── */
Document *
document_new (int width, int height)
{
    g_return_val_if_fail (width > 0 && height > 0, NULL);
    Document *doc = g_new0 (Document, 1);
    doc->width       = width;
    doc->height      = height;
    doc->active_layer = -1;
    doc->composite   = g_new0 (Pixel, (gsize)width * height);
    doc->dirty       = false;
    return doc;
}

void
document_free (Document *doc)
{
    if (!doc) return;
    for (int i = 0; i < doc->layer_count; i++)
        layer_free (doc->layers[i]);
    g_free (doc->layers);
    g_free (doc->composite);
    struct _DocumentObserverNode *n = doc->observers;
    while (n) { struct _DocumentObserverNode *next = n->next; g_free (n); n = next; }
    g_free (doc);
}

/* ── Introspection ──────────────────────────────────────────────────── */
int  document_width        (const Document *d) { return d->width; }
int  document_height       (const Document *d) { return d->height; }
int  document_layer_count  (const Document *d) { return d->layer_count; }
int  document_active_layer (const Document *d) { return d->active_layer; }
bool document_is_dirty     (const Document *d) { return d->dirty; }

Layer *
document_layer_at (const Document *d, int idx)
{
    if (idx < 0 || idx >= d->layer_count) return NULL;
    return d->layers[idx];
}

const uint8_t *
document_composite (const Document *d) { return (const uint8_t *)d->composite; }

/* ── Layer ops ──────────────────────────────────────────────────────── */
Layer *
document_add_layer (Document *doc, const char *name)
{
    if (doc->layer_count == doc->layer_capacity) {
        doc->layer_capacity = doc->layer_capacity ? doc->layer_capacity * 2 : 4;
        doc->layers = g_renew (Layer *, doc->layers, doc->layer_capacity);
    }
    Layer *l = layer_new (name, doc->width, doc->height);
    doc->layers[doc->layer_count++] = l;
    doc->active_layer = doc->layer_count - 1;   /* new layer becomes active */
    doc->dirty = true;

    document_invalidate (doc);
    notify_layer_list_changed (doc);
    notify_active_layer_changed (doc);
    return l;
}

bool
document_remove_layer (Document *doc, int idx)
{
    /* Guardrail: never allow removing the last layer — there'd be nothing
     * to draw on and the canvas would go blank with no UI to recover. */
    if (doc->layer_count <= 1) return false;
    if (idx < 0 || idx >= doc->layer_count) return false;

    layer_free (doc->layers[idx]);
    for (int i = idx; i < doc->layer_count - 1; i++)
        doc->layers[i] = doc->layers[i + 1];
    doc->layer_count--;

    if (doc->active_layer >= doc->layer_count)
        doc->active_layer = doc->layer_count - 1;
    doc->dirty = true;

    document_invalidate (doc);
    notify_layer_list_changed (doc);
    notify_active_layer_changed (doc);
    return true;
}

void
document_move_layer (Document *doc, int from, int to)
{
    if (from < 0 || from >= doc->layer_count) return;
    if (to < 0 || to >= doc->layer_count) return;
    if (from == to) return;

    Layer *moving = doc->layers[from];
    if (from < to)
        for (int i = from; i < to; i++) doc->layers[i] = doc->layers[i + 1];
    else
        for (int i = from; i > to; i--) doc->layers[i] = doc->layers[i - 1];
    doc->layers[to] = moving;

    doc->active_layer = to;
    doc->dirty = true;

    document_invalidate (doc);
    notify_layer_list_changed (doc);
    notify_active_layer_changed (doc);
}

bool
document_merge_down (Document *doc, int idx)
{
    if (idx <= 0 || idx >= doc->layer_count) return false;

    Layer *top = doc->layers[idx];
    Layer *bot = doc->layers[idx - 1];

    /* Composite top onto bottom using its opacity + blend mode. */
    gsize n = (gsize)doc->width * doc->height;
    for (gsize p = 0; p < n; p++)
        compositor_blend_pixel (&bot->buffer->pixels[p],
                                &top->buffer->pixels[p],
                                top->opacity, top->blend_mode);

    return document_remove_layer (doc, idx);
}

void
document_set_active_layer (Document *doc, int idx)
{
    if (idx < 0 || idx >= doc->layer_count) return;
    if (idx == doc->active_layer) return;
    doc->active_layer = idx;
    notify_active_layer_changed (doc);
}

void
document_layer_set_visible (Document *doc, int idx, bool visible)
{
    Layer *l = document_layer_at (doc, idx);
    if (!l || l->visible == visible) return;
    l->visible = visible;
    doc->dirty = true;
    document_invalidate (doc);
}

void
document_layer_set_opacity (Document *doc, int idx, float opacity)
{
    Layer *l = document_layer_at (doc, idx);
    if (!l) return;
    opacity = CLAMP (opacity, 0.0f, 1.0f);
    if (l->opacity == opacity) return;
    l->opacity = opacity;
    doc->dirty = true;
    document_invalidate (doc);
}

void
document_layer_set_blend_mode (Document *doc, int idx, BlendMode mode)
{
    Layer *l = document_layer_at (doc, idx);
    if (!l || l->blend_mode == mode) return;
    l->blend_mode = mode;
    doc->dirty = true;
    document_invalidate (doc);
}


/* ── Pixel mutations ────────────────────────────────────────────────── */
void
document_apply_dot (Document *doc, int cx, int cy, Pixel color, int radius)
{
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->visible || !l->buffer) return;

    int x0 = MAX (cx - radius, 0);
    int y0 = MAX (cy - radius, 0);
    int x1 = MIN (cx + radius + 1, doc->width);
    int y1 = MIN (cy + radius + 1, doc->height);

    for (int y = y0; y < y1; y++) {
        Pixel *row = &l->buffer->pixels[(gsize)y * doc->width];
        for (int x = x0; x < x1; x++)
            if (pixel_in_selection (doc, x, y)) row[x] = color;
    }

    doc->dirty = true;
    document_invalidate (doc);
}

void
document_apply_erase (Document *doc, int cx, int cy, int radius)
{
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->visible || !l->buffer) return;

    int x0 = MAX (cx - radius, 0);
    int y0 = MAX (cy - radius, 0);
    int x1 = MIN (cx + radius + 1, doc->width);
    int y1 = MIN (cy + radius + 1, doc->height);

    Pixel transparent = { 0, 0, 0, 0 };
    for (int y = y0; y < y1; y++) {
        Pixel *row = &l->buffer->pixels[(gsize)y * doc->width];
        for (int x = x0; x < x1; x++) row[x] = transparent;
    }

    doc->dirty = true;
    document_invalidate (doc);
}


void
document_apply_blur (Document *doc, int cx, int cy, int radius)
{
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->visible || !l->buffer) return;

    int w = doc->width, h = doc->height;
    int x0 = MAX (cx - radius, 0), y0 = MAX (cy - radius, 0);
    int x1 = MIN (cx + radius + 1, w), y1 = MIN (cy + radius + 1, h);
    int bx0 = MAX (x0 - 1, 0), by0 = MAX (y0 - 1, 0);
    int bx1 = MIN (x1 + 1, w), by1 = MIN (y1 + 1, h);
    int bw = bx1 - bx0, bh = by1 - by0;
    if (bw <= 0 || bh <= 0) return;

    /* Copy the halo region so 3x3 reads stay consistent. */
    Pixel *scratch = g_new (Pixel, bw * bh);
    for (int y = 0; y < bh; y++)
        memcpy (scratch + y * bw,
                &l->buffer->pixels[(by0 + y) * w + bx0],
                bw * sizeof (Pixel));

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int rr = 0, gg = 0, bb = 0, aa = 0, n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int sx = x + dx - bx0;
                    int sy = y + dy - by0;
                    if (sx >= 0 && sy >= 0 && sx < bw && sy < bh) {
                        Pixel *p = &scratch[sy * bw + sx];
                        rr += p->r; gg += p->g; bb += p->b; aa += p->a;
                        n++;
                    }
                }
            if (n > 0) {
                Pixel *dst = &l->buffer->pixels[y * w + x];
                if (pixel_in_selection (doc, x, y)) {
                    dst->r = rr / n; dst->g = gg / n;
                    dst->b = bb / n; dst->a = aa / n;
                }
            }
        }
    }
    g_free (scratch);
    doc->dirty = true;
    document_invalidate (doc);
}


/* Bresenham walk calling the per-pixel apply once, then invalidate ONCE.
 * This is ~100x faster than looping document_apply_dot on a long stroke. */
static void
bresenham_walk (int x0, int y0, int x1, int y1,
                void (*fn)(Document *, int, int, void *),
                Document *doc, void *ctx)
{
    int dx = abs (x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs (y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, cx = x0, cy = y0;
    while (1) {
        fn (doc, cx, cy, ctx);
        if (cx == x1 && cy == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
}

typedef struct { Pixel color; int r; } StampCtx;

static void
_apply_dot_raw (Document *doc, int cx, int cy, void *vctx)
{
    StampCtx *c = vctx;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->buffer) return;
    int x0 = MAX (cx - c->r, 0), y0 = MAX (cy - c->r, 0);
    int x1 = MIN (cx + c->r + 1, doc->width), y1 = MIN (cy + c->r + 1, doc->height);
    for (int y = y0; y < y1; y++) {
        Pixel *row = &l->buffer->pixels[(gsize)y * doc->width];
        for (int x = x0; x < x1; x++)
            if (pixel_in_selection (doc, x, y)) row[x] = c->color;
    }
}

static void
_apply_erase_raw (Document *doc, int cx, int cy, void *vctx)
{
    StampCtx *c = vctx;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->buffer) return;
    int x0 = MAX (cx - c->r, 0), y0 = MAX (cy - c->r, 0);
    int x1 = MIN (cx + c->r + 1, doc->width), y1 = MIN (cy + c->r + 1, doc->height);
    Pixel tr = { 0, 0, 0, 0 };
    for (int y = y0; y < y1; y++) {
        Pixel *row = &l->buffer->pixels[(gsize)y * doc->width];
        for (int x = x0; x < x1; x++)
            if (pixel_in_selection (doc, x, y)) row[x] = tr;
    }
}

static void
_apply_blur_raw (Document *doc, int cx, int cy, void *vctx)
{
    StampCtx *c = vctx;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->buffer) return;
    int w = doc->width, h = doc->height;
    int r = c->r;
    int x0 = MAX (cx - r, 0), y0 = MAX (cy - r, 0);
    int x1 = MIN (cx + r + 1, w), y1 = MIN (cy + r + 1, h);
    int bx0 = MAX (x0 - 1, 0), by0 = MAX (y0 - 1, 0);
    int bx1 = MIN (x1 + 1, w), by1 = MIN (y1 + 1, h);
    int bw = bx1 - bx0, bh = by1 - by0;
    if (bw <= 0 || bh <= 0) return;
    Pixel *scratch = g_new (Pixel, bw * bh);
    for (int y = 0; y < bh; y++)
        memcpy (scratch + y * bw, &l->buffer->pixels[(by0 + y) * w + bx0],
                bw * sizeof (Pixel));
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int rr = 0, gg = 0, bb = 0, aa = 0, n = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int sx = x + dx - bx0, sy = y + dy - by0;
                    if (sx >= 0 && sy >= 0 && sx < bw && sy < bh) {
                        Pixel *p = &scratch[sy * bw + sx];
                        rr += p->r; gg += p->g; bb += p->b; aa += p->a; n++;
                    }
                }
            if (n > 0) {
                Pixel *d = &l->buffer->pixels[y * w + x];
                d->r = rr / n; d->g = gg / n; d->b = bb / n; d->a = aa / n;
            }
        }
    g_free (scratch);
}

void
document_apply_stroke (Document *doc, int x0, int y0, int x1, int y1,
                        Pixel color, int radius)
{
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    StampCtx c = { color, MAX (radius, 1) };
    bresenham_walk (x0, y0, x1, y1, _apply_dot_raw, doc, &c);
    doc->dirty = true;
    document_invalidate (doc);
}

void
document_apply_erase_stroke (Document *doc, int x0, int y0, int x1, int y1, int radius)
{
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    StampCtx c = { {0,0,0,0}, MAX (radius, 1) };
    bresenham_walk (x0, y0, x1, y1, _apply_erase_raw, doc, &c);
    doc->dirty = true;
    document_invalidate (doc);
}

void
document_apply_blur_stroke (Document *doc, int x0, int y0, int x1, int y1, int radius)
{
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    StampCtx c = { {0,0,0,0}, MAX (radius, 1) };
    bresenham_walk (x0, y0, x1, y1, _apply_blur_raw, doc, &c);
    doc->dirty = true;
    document_invalidate (doc);
}

void
document_recomposite_all (Document *doc)
{
    document_invalidate (doc);
}

void
document_mark_clean (Document *doc)
{
    doc->dirty = false;
}


/* ── Selection implementation ───────────────────────────────────────── */
bool document_has_selection (const Document *d) { return d->has_sel; }

SelectionRect
document_selection (const Document *d)
{
    SelectionRect r = { d->sel_x, d->sel_y, d->sel_w, d->sel_h };
    return r;
}

void
document_set_selection (Document *d, int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > d->width)  w = d->width  - x;
    if (y + h > d->height) h = d->height - y;
    if (w <= 0 || h <= 0) {
        d->has_sel = false;
    } else {
        d->has_sel = true;
        d->sel_x = x; d->sel_y = y; d->sel_w = w; d->sel_h = h;
    }
    notify_selection_changed (d);
}

void
document_clear_selection (Document *d)
{
    if (!d->has_sel) return;
    d->has_sel = false;
    notify_selection_changed (d);
}

PixelBuffer *
document_copy_region (Document *doc)
{
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return NULL;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->buffer) return NULL;

    int x0 = 0, y0 = 0, w = doc->width, h = doc->height;
    if (doc->has_sel) { x0 = doc->sel_x; y0 = doc->sel_y; w = doc->sel_w; h = doc->sel_h; }

    PixelBuffer *pb = pixel_buffer_new (w, h);
    for (int y = 0; y < h; y++)
        memcpy (&pb->pixels[(gsize)y * w],
                &l->buffer->pixels[(gsize)(y0 + y) * doc->width + x0],
                (gsize)w * sizeof (Pixel));
    return pb;
}

void
document_paste_buffer (Document *doc, const PixelBuffer *buf, int x, int y)
{
    if (!buf || doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->buffer) return;

    for (int by = 0; by < buf->height; by++) {
        int dy = y + by;
        if (dy < 0 || dy >= doc->height) continue;
        for (int bx = 0; bx < buf->width; bx++) {
            int dx = x + bx;
            if (dx < 0 || dx >= doc->width) continue;
            compositor_blend_pixel (&l->buffer->pixels[(gsize)dy * doc->width + dx],
                                    &buf->pixels[(gsize)by * buf->width + bx],
                                    1.0f, BLEND_NORMAL);
        }
    }
    doc->dirty = true;
    document_invalidate (doc);
}

void
document_paste_as_layer (Document *doc, const PixelBuffer *buf, const char *name)
{
    if (!buf) return;
    Layer *l = document_add_layer (doc, name ? name : "Pasted");
    if (!l || !l->buffer) return;
    int cw = MIN (buf->width,  doc->width);
    int ch = MIN (buf->height, doc->height);
    for (int y = 0; y < ch; y++)
        memcpy (&l->buffer->pixels[(gsize)y * doc->width],
                &buf->pixels[(gsize)y * buf->width],
                (gsize)cw * sizeof (Pixel));
    document_invalidate (doc);
}

void
document_clear_region (Document *doc)
{
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->buffer) return;
    int x0 = 0, y0 = 0, w = doc->width, h = doc->height;
    if (doc->has_sel) { x0 = doc->sel_x; y0 = doc->sel_y; w = doc->sel_w; h = doc->sel_h; }
    Pixel tr = { 0, 0, 0, 0 };
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            l->buffer->pixels[(gsize)y * doc->width + x] = tr;
    doc->dirty = true;
    document_invalidate (doc);
}

/* ── Observer management ────────────────────────────────────────────── */
DocumentObserverHandle
document_add_observer (Document *doc, const DocumentObserver *obs, void *user_data)
{
    g_return_val_if_fail (doc && obs, NULL);
    struct _DocumentObserverNode *n = g_new0 (struct _DocumentObserverNode, 1);
    n->obs       = obs;
    n->user_data = user_data;
    n->next      = doc->observers;
    doc->observers = n;

    /* Push current state so a fresh observer is immediately consistent. */
    if (obs->on_composite_changed)    obs->on_composite_changed (user_data);
    if (obs->on_layer_list_changed)   obs->on_layer_list_changed (user_data);
    if (obs->on_active_layer_changed) obs->on_active_layer_changed (user_data);
    if (obs->on_selection_changed)    obs->on_selection_changed    (user_data);
    return n;
}

void
document_remove_observer (Document *doc, DocumentObserverHandle h)
{
    if (!doc || !h) return;
    struct _DocumentObserverNode **pp = &doc->observers;
    while (*pp) {
        if (*pp == h) { *pp = h->next; g_free (h); return; }
        pp = &(*pp)->next;
    }
}