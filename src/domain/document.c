#include "document.h"
#include "history.h"
#include "compositor.h"
#include "../util/log.h"
#include <glib.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

struct _DocumentObserverNode {
    const DocumentObserver *obs;
    void *user_data;
    struct _DocumentObserverNode *next;
};

struct Document {
    int width, height;
    Layer **layers;
    int layer_count, layer_capacity;
    int active_layer;
    Pixel *composite;
    struct _DocumentObserverNode *observers;
    bool dirty, has_sel;
    int sel_x, sel_y, sel_w, sel_h;
    
    CommandStack stack;
    
    /* Stroke tracking */
    PixelBuffer *stroke_before;
    Layer *stroke_layer;
    char *stroke_name;
    PixelBuffer *floating;
    int floating_x, floating_y;
};

/* ── Notifications ─────────────────────────────────────────────────── */
static void notify_composite (Document *d) {
    for (struct _DocumentObserverNode *n=d->observers;n;n=n->next)
        if (n->obs->on_composite_changed) n->obs->on_composite_changed(n->user_data);
}
static void notify_layers (Document *d) {
    for (struct _DocumentObserverNode *n=d->observers;n;n=n->next)
        if (n->obs->on_layer_list_changed) n->obs->on_layer_list_changed(n->user_data);
}
static void notify_active (Document *d) {
    for (struct _DocumentObserverNode *n=d->observers;n;n=n->next)
        if (n->obs->on_active_layer_changed) n->obs->on_active_layer_changed(n->user_data);
}
static void notify_history (Document *d) {
    for (struct _DocumentObserverNode *n=d->observers;n;n=n->next)
        if (n->obs->on_history_changed) n->obs->on_history_changed(n->user_data);
}
static void notify_selection (Document *d) {
    for (struct _DocumentObserverNode *n=d->observers;n;n=n->next)
        if (n->obs->on_selection_changed) n->obs->on_selection_changed(n->user_data);
}

static inline bool pixel_in_sel (const Document *d, int x, int y) {
    if (!d->has_sel) return true;
    return x >= d->sel_x && x < d->sel_x + d->sel_w && y >= d->sel_y && y < d->sel_y + d->sel_h;
}

/* ── Raw Layer Helpers ─────────────────────────────────────────────── */
static void doc_insert_layer_raw (Document *d, Layer *l, int index) {
    if (index < 0) index = 0;
    if (index > d->layer_count) index = d->layer_count;
    if (d->layer_count == d->layer_capacity) {
        d->layer_capacity = d->layer_capacity ? d->layer_capacity * 2 : 4;
        d->layers = g_renew(Layer*, d->layers, d->layer_capacity);
    }
    for (int i = d->layer_count; i > index; i--) d->layers[i] = d->layers[i-1];
    d->layers[index] = l;
    d->layer_count++;
}

static Layer *doc_remove_layer_raw (Document *d, int index) {
    if (index < 0 || index >= d->layer_count) return NULL;
    Layer *l = d->layers[index];
    for (int i = index; i < d->layer_count - 1; i++) d->layers[i] = d->layers[i+1];
    d->layer_count--;
    if (d->active_layer >= d->layer_count) d->active_layer = d->layer_count > 0 ? d->layer_count - 1 : -1;
    return l;
}

static int doc_find_layer (const Document *d, const Layer *l) {
    for (int i = 0; i < d->layer_count; i++) if (d->layers[i] == l) return i;
    return -1;
}

/* ── Commands ──────────────────────────────────────────────────────── */
typedef struct { Layer *layer; int index; bool in_doc; } LayerCmd;
static void cmd_add_undo (Command *c, Document *d) {
    LayerCmd *x = c->data; if (!x->in_doc) return;
    doc_remove_layer_raw(d, x->index); x->in_doc = FALSE;
    document_invalidate(d); notify_layers(d); notify_active(d);
}
static void cmd_add_redo (Command *c, Document *d) {
    LayerCmd *x = c->data; if (x->in_doc) return;
    doc_insert_layer_raw(d, x->layer, x->index); x->in_doc = TRUE;
    d->active_layer = x->index;
    document_invalidate(d); notify_layers(d); notify_active(d);
}
static void cmd_add_free (Command *c) {
    LayerCmd *x = c->data; if (!x->in_doc) layer_free(x->layer); g_free(x);
}

typedef struct { int from, to; } MoveCmd;
static void cmd_move_undo (Command *c, Document *d) {
    MoveCmd *x = c->data;
    Layer *l = d->layers[x->to];
    for (int i = x->to; i > x->from; i--) d->layers[i] = d->layers[i-1];
    d->layers[x->from] = l;
    d->active_layer = x->from;
    document_invalidate(d); notify_layers(d); notify_active(d);
}
static void cmd_move_redo (Command *c, Document *d) {
    MoveCmd *x = c->data;
    Layer *l = d->layers[x->from];
    for (int i = x->from; i < x->to; i++) d->layers[i] = d->layers[i+1];
    d->layers[x->to] = l;
    d->active_layer = x->to;
    document_invalidate(d); notify_layers(d); notify_active(d);
}
static void cmd_move_free (Command *c) { g_free(c->data); }

typedef struct {
    Layer *top; int top_index;
    PixelBuffer *bottom_before; Layer *bottom;
    bool top_in_doc;
} MergeCmd;
static void cmd_merge_undo (Command *c, Document *d) {
    MergeCmd *x = c->data;
    memcpy(x->bottom->buffer->pixels, x->bottom_before->pixels, (gsize)d->width*d->height*sizeof(Pixel));
    if (!x->top_in_doc) { doc_insert_layer_raw(d, x->top, x->top_index); x->top_in_doc = TRUE; }
    d->active_layer = x->top_index;
    document_invalidate(d); notify_layers(d); notify_active(d);
}
static void cmd_merge_redo (Command *c, Document *d) {
    MergeCmd *x = c->data;
    memcpy(x->bottom->buffer->pixels, x->bottom_before->pixels, (gsize)d->width*d->height*sizeof(Pixel));
    gsize n = (gsize)d->width * d->height;
    for (gsize p = 0; p < n; p++)
        compositor_blend_pixel(&x->bottom->buffer->pixels[p], &x->top->buffer->pixels[p], x->top->opacity, x->top->blend_mode);
    if (x->top_in_doc) {
        int idx = doc_find_layer(d, x->top);
        if (idx >= 0) doc_remove_layer_raw(d, idx);
        x->top_in_doc = FALSE;
    }
    d->active_layer = x->top_index - 1;
    document_invalidate(d); notify_layers(d); notify_active(d);
}
static void cmd_merge_free (Command *c) {
    MergeCmd *x = c->data; if (!x->top_in_doc) layer_free(x->top);
    pixel_buffer_free(x->bottom_before); g_free(x);
}

typedef struct {
    Layer *layer; int rx, ry, rw, rh;
    PixelBuffer *before, *after;
} RegionCmd;
static void region_apply (RegionCmd *x, const PixelBuffer *src) {
    for (int y = 0; y < x->rh; y++)
        memcpy(&x->layer->buffer->pixels[(gsize)(x->ry+y)*x->layer->buffer->width + x->rx],
               &src->pixels[(gsize)y*x->rw], (gsize)x->rw * sizeof(Pixel));
}
static void cmd_region_undo (Command *c, Document *d) { region_apply(c->data, ((RegionCmd*)c->data)->before); document_invalidate(d); }
static void cmd_region_redo (Command *c, Document *d) { region_apply(c->data, ((RegionCmd*)c->data)->after);  document_invalidate(d); }
static void cmd_region_free (Command *c) {
    RegionCmd *x = c->data; pixel_buffer_free(x->before); pixel_buffer_free(x->after); g_free(x);
}

typedef struct {
    Layer *layer; int rx, ry, rw, rh;
    PixelBuffer *before, *after;
} StrokeCmd;
static void cmd_stroke_apply (StrokeCmd *x, const PixelBuffer *src) {
    for (int y = 0; y < x->rh; y++)
        memcpy(&x->layer->buffer->pixels[(gsize)(x->ry+y)*x->layer->buffer->width + x->rx],
               &src->pixels[(gsize)y*x->rw], (gsize)x->rw * sizeof(Pixel));
}
static void cmd_stroke_undo (Command *c, Document *d) { cmd_stroke_apply(c->data, ((StrokeCmd*)c->data)->before); document_invalidate(d); }
static void cmd_stroke_redo (Command *c, Document *d) { cmd_stroke_apply(c->data, ((StrokeCmd*)c->data)->after);  document_invalidate(d); }
static void cmd_stroke_free (Command *c) {
    StrokeCmd *x = c->data; pixel_buffer_free(x->before); pixel_buffer_free(x->after); g_free(x);
}

/* ── Core Lifecycle ────────────────────────────────────────────────── */
Document *document_new (int w, int h) {
    Document *d = g_new0(Document, 1);
    d->width = w; d->height = h; d->active_layer = -1;
    d->composite = g_new0(Pixel, (gsize)w * h);
    command_stack_init(&d->stack);
    return d;
}

void document_free (Document *d) {
    if (!d) return;
    command_stack_clear(&d->stack);
    for (int i = 0; i < d->layer_count; i++) layer_free(d->layers[i]);
    g_free(d->layers); g_free(d->composite);
    if (d->stroke_before) pixel_buffer_free(d->stroke_before);
    g_free(d);
}

int document_width (const Document *d) { return d->width; }
int document_height (const Document *d) { return d->height; }
int document_layer_count (const Document *d) { return d->layer_count; }
int document_active_layer (const Document *d) { return d->active_layer; }
bool document_is_dirty (const Document *d) { return d->dirty; }
Layer *document_layer_at (const Document *d, int i) { return (i<0||i>=d->layer_count)?NULL:d->layers[i]; }
const uint8_t *document_composite (const Document *d) { return (const uint8_t*)d->composite; }

void document_invalidate (Document *d) {
    compositor_composite(d->composite, d->layers, d->layer_count, d->width, d->height);
    notify_composite(d);
}

/* ── Layer Ops ─────────────────────────────────────────────────────── */
Layer *document_add_layer (Document *d, const char *name) {
    Layer *l = layer_new(name, d->width, d->height);
    doc_insert_layer_raw(d, l, d->layer_count);
    d->active_layer = d->layer_count - 1;
    
    Command *c = command_new("Add Layer", sizeof(LayerCmd));
    LayerCmd *x = g_new0(LayerCmd, 1); x->layer = l; x->index = d->active_layer; x->in_doc = TRUE;
    c->data = x; c->undo = cmd_add_undo; c->redo = cmd_add_redo; c->free = cmd_add_free;
    command_stack_push(&d->stack, c);

    document_invalidate(d); notify_layers(d); notify_active(d);
    return l;
}

Layer *document_duplicate_layer (Document *d, int idx) {
    Layer *src = document_layer_at(d, idx); if (!src) return NULL;
    char *name = g_strdup_printf("%s copy", src->name);
    Layer *l = layer_new(name, d->width, d->height); g_free(name);
    l->opacity = src->opacity; l->blend_mode = src->blend_mode; l->visible = src->visible;
    memcpy(l->buffer->pixels, src->buffer->pixels, (gsize)d->width*d->height*sizeof(Pixel));
    
    doc_insert_layer_raw(d, l, idx + 1);
    d->active_layer = idx + 1;

    Command *c = command_new("Duplicate Layer", sizeof(LayerCmd));
    LayerCmd *x = g_new0(LayerCmd, 1); x->layer = l; x->index = d->active_layer; x->in_doc = TRUE;
    c->data = x; c->undo = cmd_add_undo; c->redo = cmd_add_redo; c->free = cmd_add_free;
    command_stack_push(&d->stack, c);

    document_invalidate(d); notify_layers(d); notify_active(d);
    return l;
}

bool document_remove_layer (Document *d, int idx) {
    if (idx < 0 || idx >= d->layer_count || d->layer_count <= 1) return false;
    Layer *l = doc_remove_layer_raw(d, idx);
    
    Command *c = command_new("Delete Layer", sizeof(LayerCmd));
    LayerCmd *x = g_new0(LayerCmd, 1); x->layer = l; x->index = idx; x->in_doc = FALSE;
    c->data = x; c->undo = cmd_add_redo; c->redo = cmd_add_undo; c->free = cmd_add_free;
    command_stack_push(&d->stack, c);

    document_invalidate(d); notify_layers(d); notify_active(d);
    return true;
}

void document_move_layer (Document *d, int from, int to) {
    if (from < 0 || from >= d->layer_count || to < 0 || to >= d->layer_count || from == to) return;
    Layer *l = d->layers[from];
    if (from < to) for (int i = from; i < to; i++) d->layers[i] = d->layers[i+1];
    else for (int i = from; i > to; i--) d->layers[i] = d->layers[i-1];
    d->layers[to] = l;
    d->active_layer = to;

    Command *c = command_new("Move Layer", sizeof(MoveCmd));
    MoveCmd *x = g_new0(MoveCmd, 1); x->from = from; x->to = to;
    c->data = x; c->undo = cmd_move_undo; c->redo = cmd_move_redo; c->free = cmd_move_free;
    command_stack_push(&d->stack, c);

    document_invalidate(d); notify_layers(d); notify_active(d);
}

bool document_merge_down (Document *d, int idx) {
    if (idx <= 0 || idx >= d->layer_count) return false;
    Layer *top = d->layers[idx]; Layer *bot = d->layers[idx-1];
    PixelBuffer *snap = pixel_buffer_new(d->width, d->height);
    memcpy(snap->pixels, bot->buffer->pixels, (gsize)d->width*d->height*sizeof(Pixel));

    gsize n = (gsize)d->width * d->height;
    for (gsize p = 0; p < n; p++)
        compositor_blend_pixel(&bot->buffer->pixels[p], &top->buffer->pixels[p], top->opacity, top->blend_mode);
    doc_remove_layer_raw(d, idx);

    Command *c = command_new("Merge Down", sizeof(MergeCmd) + (gsize)d->width*d->height*4);
    MergeCmd *x = g_new0(MergeCmd, 1);
    x->top = top; x->top_index = idx; x->bottom = bot; x->bottom_before = snap; x->top_in_doc = FALSE;
    c->data = x; c->undo = cmd_merge_undo; c->redo = cmd_merge_redo; c->free = cmd_merge_free;
    command_stack_push(&d->stack, c);

    document_invalidate(d); notify_layers(d); notify_active(d);
    return true;
}

void document_set_active_layer (Document *d, int idx) {
    if (idx < 0 || idx >= d->layer_count || idx == d->active_layer) return;
    d->active_layer = idx; notify_active(d);
}

void document_layer_set_visible (Document *d, int idx, bool v) {
    Layer *l = document_layer_at(d, idx); if (!l || l->visible == v) return;
    l->visible = v; document_invalidate(d); notify_layers(d);
}
void document_layer_set_opacity (Document *d, int idx, float o) {
    Layer *l = document_layer_at(d, idx); if (!l) return;
    l->opacity = CLAMP(o, 0.0f, 1.0f); document_invalidate(d); notify_layers(d);
}
void document_layer_set_blend_mode (Document *d, int idx, BlendMode m) {
    Layer *l = document_layer_at(d, idx); if (!l || l->blend_mode == m) return;
    l->blend_mode = m; document_invalidate(d); notify_layers(d);
}

/* ── Stroke Tracking & History ─────────────────────────────────────── */
void document_begin_stroke (Document *d, const char *name) {
    if (d->stroke_name) g_free(d->stroke_name);
    d->stroke_name = g_strdup(name ? name : "Paint");
    if (d->stroke_before) return;
    if (d->active_layer < 0 || d->active_layer >= d->layer_count) return;
    Layer *l = d->layers[d->active_layer];
    if (!l || !l->buffer) return;
    d->stroke_layer = l;
    d->stroke_before = pixel_buffer_new(d->width, d->height);
    memcpy(d->stroke_before->pixels, l->buffer->pixels, (gsize)d->width * d->height * sizeof(Pixel));
}

void document_end_stroke (Document *d) {
    if (!d->stroke_before || !d->stroke_layer) return;
    
    // SAFETY: If the canvas was resized during the stroke, abort to prevent heap corruption
    if (d->stroke_before->width != d->width || d->stroke_before->height != d->height) {
        LOG_WARN("Stroke aborted: canvas size changed during stroke (was %dx%d, now %dx%d)",
                 d->stroke_before->width, d->stroke_before->height, d->width, d->height);
        pixel_buffer_free(d->stroke_before);
        d->stroke_before = NULL;
        d->stroke_layer = NULL;
        if (d->stroke_name) { g_free(d->stroke_name); d->stroke_name = NULL; }
        return;
    }
    
    Layer *l = d->stroke_layer;
    
    int minx = d->width, miny = d->height, maxx = -1, maxy = -1;
    const Pixel *before = d->stroke_before->pixels;
    const Pixel *after = l->buffer->pixels;
    for (int y = 0; y < d->height; y++) {
        for (int x = 0; x < d->width; x++) {
            const Pixel *a = &before[(gsize)y * d->width + x];
            const Pixel *b = &after[(gsize)y * d->width + x];
            if (a->r!=b->r || a->g!=b->g || a->b!=b->b || a->a!=b->a) {
                if (x < minx) minx = x; 
                if (y < miny) miny = y;
                if (x > maxx) maxx = x; 
                if (y > maxy) maxy = y;
            }
        }
    }

    if (maxx >= minx && maxy >= miny) {
        int rw = maxx - minx + 1, rh = maxy - miny + 1;
        PixelBuffer *cb = pixel_buffer_new(rw, rh);
        PixelBuffer *ca = pixel_buffer_new(rw, rh);
        for (int y = 0; y < rh; y++) {
            memcpy(&cb->pixels[(gsize)y*rw], &before[(gsize)(miny+y)*d->width + minx], (gsize)rw*sizeof(Pixel));
            memcpy(&ca->pixels[(gsize)y*rw], &after [(gsize)(miny+y)*d->width + minx], (gsize)rw*sizeof(Pixel));
        }
        StrokeCmd *x = g_new0(StrokeCmd, 1);
        x->layer = l; x->rx = minx; x->ry = miny; x->rw = rw; x->rh = rh;
        x->before = cb; x->after = ca;
        Command *c = command_new(d->stroke_name ? d->stroke_name : "Paint", sizeof(StrokeCmd) + 2 * (gsize)rw * rh * 4);
        if (d->stroke_name) { g_free(d->stroke_name); d->stroke_name = NULL; }
        c->data = x; c->undo = cmd_stroke_undo; c->redo = cmd_stroke_redo; c->free = cmd_stroke_free;
        command_stack_push(&d->stack, c);
    }
    notify_history(d);
    pixel_buffer_free(d->stroke_before);
    d->stroke_before = NULL; d->stroke_layer = NULL;
}

bool document_undo (Document *d) {
    bool res = command_stack_undo(&d->stack, d);
    if (res) notify_history(d);
    return res;
}
bool document_redo (Document *d) {
    bool res = command_stack_redo(&d->stack, d);
    if (res) notify_history(d);
    return res;
}
const char *document_undo_label (const Document *d) { return command_stack_undo_label(&d->stack); }
const char *document_redo_label (const Document *d) { return command_stack_redo_label(&d->stack); }

/* ── Fast Drawing (NO invalidation here) ───────────────────────────── */
static void apply_dot_raw(Document *d, int cx, int cy, Pixel color, int r) {
    if (d->active_layer < 0 || d->active_layer >= d->layer_count) return;
    Layer *l = d->layers[d->active_layer]; if (!l || !l->visible || !l->buffer) return;
    int x0 = MAX(cx-r, 0), y0 = MAX(cy-r, 0);
    int x1 = MIN(cx+r+1, d->width), y1 = MIN(cy+r+1, d->height);
    for (int y = y0; y < y1; y++) {
        Pixel *row = &l->buffer->pixels[(gsize)y * d->width];
        for (int x = x0; x < x1; x++) if (pixel_in_sel(d, x, y)) row[x] = color;
    }
}

static void apply_blur_raw(Document *d, int cx, int cy, int r) {
    if (d->active_layer < 0 || d->active_layer >= d->layer_count) return;
    Layer *l = d->layers[d->active_layer]; if (!l || !l->visible || !l->buffer) return;
    int w = d->width, h = d->height;
    int x0 = MAX(cx-r, 0), y0 = MAX(cy-r, 0), x1 = MIN(cx+r+1, w), y1 = MIN(cy+r+1, h);
    int bw = x1-x0, bh = y1-y0; if (bw <= 0 || bh <= 0) return;
    Pixel *scratch = g_new(Pixel, bw * bh);
    for (int y = 0; y < bh; y++) memcpy(&scratch[y*bw], &l->buffer->pixels[(y0+y)*w + x0], bw*sizeof(Pixel));
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        int rr=0,gg=0,bb=0,aa=0,n=0;
        for (int dy=-1; dy<=1; dy++) for (int dx=-1; dx<=1; dx++) {
            int sx=x+dx-x0, sy=y+dy-y0;
            if (sx>=0 && sy>=0 && sx<bw && sy<bh) { Pixel *p=&scratch[sy*bw+sx]; rr+=p->r; gg+=p->g; bb+=p->b; aa+=p->a; n++; }
        }
        if (n>0 && pixel_in_sel(d,x,y)) { Pixel *dst=&l->buffer->pixels[y*w+x]; dst->r=rr/n; dst->g=gg/n; dst->b=bb/n; dst->a=aa/n; }
    }
    g_free(scratch);
}

void document_apply_dot (Document *d, int cx, int cy, Pixel c, int r) { apply_dot_raw(d,cx,cy,c,r); d->dirty=true; }



void document_apply_brush_blend (Document *doc, int cx, int cy, int radius, float hardness, Pixel color, int blend_mode) {
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->visible || !l->buffer) return;

    int x0 = MAX (cx - radius, 0), y0 = MAX (cy - radius, 0);
    int x1 = MIN (cx + radius + 1, doc->width), y1 = MIN (cy + radius + 1, doc->height);

    BlendMode mode = BLEND_NORMAL;
    if (blend_mode == 1) mode = BLEND_MULTIPLY;
    else if (blend_mode == 2) mode = BLEND_SCREEN;
    else if (blend_mode == 3) mode = BLEND_OVERLAY;
    else if (blend_mode == 4) mode = BLEND_DIFFERENCE;
    else if (blend_mode == 5) mode = BLEND_ADDITION;

    float radius_sq = (float)(radius * radius);
    float edge_r = radius * hardness;
    float edge_sq = edge_r * edge_r;

    for (int y = y0; y < y1; y++) {
        Pixel *row = &l->buffer->pixels[(gsize)y * doc->width];
        for (int x = x0; x < x1; x++) {
            if (!pixel_in_sel(doc, x, y)) continue;
            
            float dx = (float)(x - cx);
            float dy = (float)(y - cy);
            float dist_sq = dx*dx + dy*dy;
            
            if (dist_sq <= radius_sq) {
                float alpha_mult = 1.0f;
                if (dist_sq > edge_sq && radius_sq > edge_sq) {
                    float t = (dist_sq - edge_sq) / (radius_sq - edge_sq);
                    alpha_mult = 1.0f - t;
                }
                
                if (alpha_mult > 0.001f) {
                    Pixel src = color;
                    src.a = (uint8_t)(src.a * alpha_mult);
                    compositor_blend_pixel(&row[x], &src, 1.0f, mode);
                }
            }
        }
    }
    doc->dirty = true;
}

void document_apply_soft_erase (Document *doc, int cx, int cy, int radius, float hardness, float opacity) {
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->visible || !l->buffer) return;

    int x0 = MAX (cx - radius, 0), y0 = MAX (cy - radius, 0);
    int x1 = MIN (cx + radius + 1, doc->width), y1 = MIN (cy + radius + 1, doc->height);
    float radius_sq = (float)(radius * radius);
    float edge_r = radius * hardness;
    float edge_sq = edge_r * edge_r;

    for (int y = y0; y < y1; y++) {
        Pixel *row = &l->buffer->pixels[(gsize)y * doc->width];
        for (int x = x0; x < x1; x++) {
            if (!pixel_in_sel(doc, x, y)) continue;
            
            float dx = (float)(x - cx);
            float dy = (float)(y - cy);
            float dist_sq = dx*dx + dy*dy;
            
            if (dist_sq <= radius_sq) {
                float alpha_mult = 1.0f;
                if (dist_sq > edge_sq && radius_sq > edge_sq) {
                    float t = (dist_sq - edge_sq) / (radius_sq - edge_sq);
                    alpha_mult = 1.0f - t;
                }
                alpha_mult *= opacity;
                
                if (alpha_mult > 0.001f) {
                    float scale = 1.0f - alpha_mult;
                    row[x].a = (uint8_t)(row[x].a * scale);
                    if (row[x].a == 0) row[x].r = row[x].g = row[x].b = 0;
                }
            }
        }
    }
    doc->dirty = true;
}

void document_apply_brush (Document *doc, int cx, int cy, int radius, float hardness, float opacity, Pixel color) {
    if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return;
    Layer *l = doc->layers[doc->active_layer];
    if (!l || !l->visible || !l->buffer) return;

    int x0 = MAX (cx - radius, 0), y0 = MAX (cy - radius, 0);
    int x1 = MIN (cx + radius + 1, doc->width), y1 = MIN (cy + radius + 1, doc->height);
    float radius_sq = (float)(radius * radius);
    float edge_r = radius * hardness;
    float edge_sq = edge_r * edge_r;

    for (int y = y0; y < y1; y++) {
        Pixel *row = &l->buffer->pixels[(gsize)y * doc->width];
        for (int x = x0; x < x1; x++) {
            if (!pixel_in_sel(doc, x, y)) continue;
            
            float dx = (float)(x - cx);
            float dy = (float)(y - cy);
            float dist_sq = dx*dx + dy*dy;
            
            if (dist_sq <= radius_sq) {
                /* Linear falloff approximation using squared distance
                 * (avoids sqrtf so we don't need libm on Linux) */
                float alpha_mult = 1.0f;
                if (dist_sq > edge_sq && radius_sq > edge_sq) {
                    float t = (dist_sq - edge_sq) / (radius_sq - edge_sq);
                    /* Convert squared t to linear for smoother falloff */
                    alpha_mult = 1.0f - t;
                }
                alpha_mult *= opacity;
                
                if (alpha_mult > 0.001f) {
                    Pixel src = color;
                    src.a = (uint8_t)(src.a * alpha_mult);
                    float sa = src.a / 255.0f;
                    float da = row[x].a / 255.0f;
                    float out_a = sa + da * (1.0f - sa);
                    
                    if (out_a > 0.0f) {
                        row[x].r = (uint8_t)((src.r * sa + row[x].r * da * (1.0f - sa)) / out_a);
                        row[x].g = (uint8_t)((src.g * sa + row[x].g * da * (1.0f - sa)) / out_a);
                        row[x].b = (uint8_t)((src.b * sa + row[x].b * da * (1.0f - sa)) / out_a);
                        row[x].a = (uint8_t)(out_a * 255.0f);
                    }
                }
            }
        }
    }
    doc->dirty = true;
}

void document_apply_erase (Document *d, int cx, int cy, int r) { apply_dot_raw(d,cx,cy,(Pixel){0,0,0,0},r); d->dirty=true; }
void document_apply_blur (Document *d, int cx, int cy, int r) { apply_blur_raw(d,cx,cy,r); d->dirty=true; }

static void stroke_line(Document *d, int x0, int y0, int x1, int y1, Pixel color, int r, int mode) {
    int dx=abs(x1-x0), sx=x0<x1?1:-1, dy=-abs(y1-y0), sy=y0<y1?1:-1, err=dx+dy, cx=x0, cy=y0;
    while (1) {
        if (mode == 0) apply_dot_raw(d, cx, cy, color, r);
        else if (mode == 1) apply_dot_raw(d, cx, cy, (Pixel){0,0,0,0}, r);
        else apply_blur_raw(d, cx, cy, r);
        if (cx==x1 && cy==y1) break;
        int e2=2*err; if (e2>=dy) { err+=dy; cx+=sx; } if (e2<=dx) { err+=dx; cy+=sy; }
    }
    d->dirty = true;
}

void document_apply_stroke (Document *d, int x0, int y0, int x1, int y1, Pixel c, int r) { stroke_line(d,x0,y0,x1,y1,c,r,0); }
void document_apply_erase_stroke (Document *d, int x0, int y0, int x1, int y1, int r) { stroke_line(d,x0,y0,x1,y1,(Pixel){0,0,0,0},r,1); }
void document_apply_blur_stroke (Document *d, int x0, int y0, int x1, int y1, int r) { stroke_line(d,x0,y0,x1,y1,(Pixel){0,0,0,0},r,2); }

/* ── Selection & Misc ──────────────────────────────────────────────── */
void document_mark_clean (Document *d) { d->dirty = false; }
bool document_has_selection (const Document *d) { return d->has_sel; }
SelectionRect document_selection (const Document *d) { SelectionRect r={d->sel_x,d->sel_y,d->sel_w,d->sel_h}; return r; }

void document_set_selection (Document *d, int x, int y, int w, int h) {
    if (x<0) { w+=x; x=0; }
    if (y<0) { h+=y; y=0; }
    if (x+w>d->width) w=d->width-x; 
    if (y+h>d->height) h=d->height-y;
    if (w<=0 || h<=0) d->has_sel=false; 
    else { d->has_sel=true; d->sel_x=x; d->sel_y=y; d->sel_w=w; d->sel_h=h; }
    notify_selection(d);
}

void document_clear_selection (Document *d) { if (d->has_sel) { d->has_sel=false; notify_selection(d); } }

PixelBuffer *document_copy_region (Document *d) {
    if (d->active_layer < 0 || d->active_layer >= d->layer_count) return NULL;
    Layer *l = d->layers[d->active_layer]; if (!l || !l->buffer) return NULL;
    int x0 = 0, y0 = 0, w = d->width, h = d->height;
    if (d->has_sel) { x0 = d->sel_x; y0 = d->sel_y; w = d->sel_w; h = d->sel_h; }
    PixelBuffer *pb = pixel_buffer_new(w, h);
    for (int y = 0; y < h; y++)
        memcpy(&pb->pixels[(gsize)y*w], &l->buffer->pixels[(gsize)(y0+y)*d->width + x0], (gsize)w*sizeof(Pixel));
    return pb;
}

void document_clear_region (Document *d) {
    if (d->active_layer < 0 || d->active_layer >= d->layer_count) return;
    Layer *l = d->layers[d->active_layer]; if (!l || !l->buffer) return;
    int x0 = 0, y0 = 0, w = d->width, h = d->height;
    if (d->has_sel) { x0 = d->sel_x; y0 = d->sel_y; w = d->sel_w; h = d->sel_h; }
    if (w <= 0 || h <= 0) return;

    PixelBuffer *before = pixel_buffer_new(w, h);
    for (int y = 0; y < h; y++)
        memcpy(&before->pixels[(gsize)y*w], &l->buffer->pixels[(gsize)(y0+y)*d->width + x0], (gsize)w*sizeof(Pixel));

    Pixel tr = { 0, 0, 0, 0 };
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) l->buffer->pixels[(gsize)y*d->width + x] = tr;

    PixelBuffer *after = pixel_buffer_new(w, h);
    for (int y = 0; y < h; y++)
        memcpy(&after->pixels[(gsize)y*w], &l->buffer->pixels[(gsize)(y0+y)*d->width + x0], (gsize)w*sizeof(Pixel));

    RegionCmd *x = g_new0(RegionCmd, 1);
    x->layer = l; x->rx = x0; x->ry = y0; x->rw = w; x->rh = h;
    x->before = before; x->after = after;
    Command *c = command_new("Clear", sizeof(RegionCmd) + 2*(gsize)w*h*4);
    c->data = x; c->undo = cmd_region_undo; c->redo = cmd_region_redo; c->free = cmd_region_free;
    command_stack_push(&d->stack, c);

    d->dirty = true;
    document_invalidate(d);
}

void document_paste_buffer (Document *d, const PixelBuffer *buf, int px, int py) {
    if (!buf || d->active_layer < 0 || d->active_layer >= d->layer_count) return;
    Layer *l = d->layers[d->active_layer]; if (!l || !l->buffer) return;
    int x0 = MAX(px, 0), y0 = MAX(py, 0), x1 = MIN(px+buf->width, d->width), y1 = MIN(py+buf->height, d->height);
    int rw = x1 - x0, rh = y1 - y0;
    if (rw <= 0 || rh <= 0) return;

    PixelBuffer *before = pixel_buffer_new(rw, rh);
    for (int y = 0; y < rh; y++)
        memcpy(&before->pixels[(gsize)y*rw], &l->buffer->pixels[(gsize)(y0+y)*d->width + x0], (gsize)rw*sizeof(Pixel));

    for (int by = 0; by < buf->height; by++) {
        int dy = py + by; if (dy < 0 || dy >= d->height) continue;
        for (int bx = 0; bx < buf->width; bx++) {
            int dx = px + bx; if (dx < 0 || dx >= d->width) continue;
            compositor_blend_pixel(&l->buffer->pixels[(gsize)dy*d->width + dx], &buf->pixels[(gsize)by*buf->width + bx], 1.0f, BLEND_NORMAL);
        }
    }

    PixelBuffer *after = pixel_buffer_new(rw, rh);
    for (int y = 0; y < rh; y++)
        memcpy(&after->pixels[(gsize)y*rw], &l->buffer->pixels[(gsize)(y0+y)*d->width + x0], (gsize)rw*sizeof(Pixel));

    RegionCmd *x = g_new0(RegionCmd, 1);
    x->layer = l; x->rx = x0; x->ry = y0; x->rw = rw; x->rh = rh;
    x->before = before; x->after = after;
    Command *c = command_new("Paste", sizeof(RegionCmd) + 2*(gsize)rw*rh*4);
    c->data = x; c->undo = cmd_region_undo; c->redo = cmd_region_redo; c->free = cmd_region_free;
    command_stack_push(&d->stack, c);

    d->dirty = true;
    document_invalidate(d);
}

void document_paste_as_layer (Document *d, const PixelBuffer *buf, const char *name) {
    if (!buf) return;
    Layer *l = document_add_layer(d, name ? name : "Pasted");
    if (!l || !l->buffer) return;
    int cw = MIN(buf->width, d->width), ch = MIN(buf->height, d->height);
    for (int y = 0; y < ch; y++)
        memcpy(&l->buffer->pixels[(gsize)y*d->width], &buf->pixels[(gsize)y*buf->width], (gsize)cw*sizeof(Pixel));
    document_invalidate(d); notify_layers(d);
}

DocumentObserverHandle document_add_observer (Document *d, const DocumentObserver *o, void *ud) {
    struct _DocumentObserverNode *n = g_new0(struct _DocumentObserverNode, 1);
    n->obs=o; n->user_data=ud; n->next=d->observers; d->observers=n;
    if (o->on_composite_changed) o->on_composite_changed(ud);
    if (o->on_layer_list_changed) o->on_layer_list_changed(ud);
    if (o->on_active_layer_changed) o->on_active_layer_changed(ud);
    return n;
}
void document_remove_observer (Document *d, DocumentObserverHandle h) {
    struct _DocumentObserverNode **pp = &d->observers;
    while (*pp) { if (*pp == h) { *pp = h->next; g_free(h); return; } pp=&(*pp)->next; }
}

int document_history_length (const Document *d) { return command_stack_length(&d->stack); }
int document_history_current_index (const Document *d) { return command_stack_current_index(&d->stack); }
const char *document_history_label_at (const Document *d, int idx) {
    Command *c = command_stack_get(&d->stack, idx);
    return c ? c->label : "";
}









void document_set_floating(Document *doc, PixelBuffer *buf, int x, int y) {
    doc->floating = buf;
    doc->floating_x = x;
    doc->floating_y = y;
    document_invalidate(doc);
}

PixelBuffer *document_get_floating(Document *doc, int *x, int *y) {
    if (x) *x = doc->floating_x;
    if (y) *y = doc->floating_y;
    return doc->floating;
}

void document_resize(Document *doc, int nw, int nh) {
    if (nw <= 0 || nh <= 0 || (nw == doc->width && nh == doc->height)) return;
    
    // FIX: Abort any active stroke to prevent out-of-bounds reads in document_end_stroke
    if (doc->stroke_before) {
        pixel_buffer_free(doc->stroke_before);
        doc->stroke_before = NULL;
        doc->stroke_layer = NULL;
    }
    if (doc->stroke_name) {
        g_free(doc->stroke_name);
        doc->stroke_name = NULL;
    }
    
    Pixel *new_comp = g_new0(Pixel, (gsize)nw * nh);
    for (int i = 0; i < doc->layer_count; i++) {
        Layer *l = doc->layers[i];
        PixelBuffer *new_buf = pixel_buffer_new(nw, nh);
        int cw = MIN(doc->width, nw);
        int ch = MIN(doc->height, nh);
        for (int y = 0; y < ch; y++) {
            memcpy(&new_buf->pixels[(gsize)y * nw], &l->buffer->pixels[(gsize)y * doc->width], (gsize)cw * sizeof(Pixel));
        }
        pixel_buffer_free(l->buffer);
        l->buffer = new_buf;
    }
    g_free(doc->composite);
    doc->composite = new_comp;
    doc->width = nw;
    doc->height = nh;
    
    doc->has_sel = false;
    
    document_invalidate(doc);
    notify_layers(doc);
    notify_selection(doc);
}

void document_clear_history (Document *doc) {
    command_stack_clear(&doc->stack);
    doc->dirty = false;
}

