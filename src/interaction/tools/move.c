#include "move.h"
#include "../input_event.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    int drag_start_x, drag_start_y;       // Where mouse clicked for THIS drag
    int sel_start_x, sel_start_y;         // Selection pos at start of THIS drag
    int sel_start_w, sel_start_h;         // Selection size at start of THIS drag
    int hole_x, hole_y, hole_w, hole_h;   // Original position where pixels were cut (NEVER changes)
    
    int resize_edge;
    gboolean dragging;
    gboolean active;
    
    PixelBuffer *floating;                // Current scaled pixels being dragged
    PixelBuffer *orig_floating;           // Original un-scaled pixels
    PixelBuffer *backup_layer;            // The layer before the transform started
    int layer_w, layer_h;
} MoveData;

static void move_commit(Tool *tool, Document *doc);

static PixelBuffer *
scale_buffer_bilinear (const PixelBuffer *src, int nw, int nh)
{
    PixelBuffer *dst = pixel_buffer_new(nw, nh);
    if (!src || nw <= 0 || nh <= 0 || src->width == 0 || src->height == 0) return dst;
    for (int y = 0; y < nh; y++) {
        for (int x = 0; x < nw; x++) {
            float gx = (nw > 1) ? (float)x / (nw - 1) * (src->width - 1) : 0;
            float gy = (nh > 1) ? (float)y / (nh - 1) * (src->height - 1) : 0;
            int x0 = (int)gx, y0 = (int)gy;
            int x1 = x0 + 1, y1 = y0 + 1;
            if (x1 >= src->width) x1 = src->width - 1;
            if (y1 >= src->height) y1 = src->height - 1;
            float wx = gx - x0, wy = gy - y0;
            
            Pixel p00 = src->pixels[y0 * src->width + x0];
            Pixel p10 = src->pixels[y0 * src->width + x1];
            Pixel p01 = src->pixels[y1 * src->width + x0];
            Pixel p11 = src->pixels[y1 * src->width + x1];
            
            #define LERP(a, b, t) ((a) + ((b) - (a)) * (t))
            dst->pixels[y * nw + x].r = LERP(LERP(p00.r, p10.r, wx), LERP(p01.r, p11.r, wx), wy);
            dst->pixels[y * nw + x].g = LERP(LERP(p00.g, p10.g, wx), LERP(p01.g, p11.g, wx), wy);
            dst->pixels[y * nw + x].b = LERP(LERP(p00.b, p10.b, wx), LERP(p01.b, p11.b, wx), wy);
            dst->pixels[y * nw + x].a = LERP(LERP(p00.a, p10.a, wx), LERP(p01.a, p11.a, wx), wy);
        }
    }
    return dst;
}

static void move_activate(Tool *tool, Document *doc) {
    (void)doc; MoveData *md = tool->user_data;
    md->active = FALSE; md->dragging = FALSE;
    if (md->floating) { pixel_buffer_free(md->floating); md->floating = NULL; }
    if (md->orig_floating) { pixel_buffer_free(md->orig_floating); md->orig_floating = NULL; }
}

static void move_begin(Tool *tool, Document *doc, InputEvent *ev) {
    MoveData *md = tool->user_data;
    if (!document_has_selection(doc)) return;

    SelectionRect r = document_selection(doc);
    int margin = 8;
    int in_left = (ev->x > r.x - margin && ev->x < r.x + margin);
    int in_right = (ev->x > r.x + r.w - margin && ev->x < r.x + r.w + margin);
    int in_top = (ev->y > r.y - margin && ev->y < r.y + margin);
    int in_bottom = (ev->y > r.y + r.h - margin && ev->y < r.y + r.h + margin);
    int inside_x = (ev->x >= r.x && ev->x <= r.x + r.w);
    int inside_y = (ev->y >= r.y && ev->y <= r.y + r.h);

    int hit = 0;
    if (in_left && in_top) hit = 6;          // Top-Left
    else if (in_right && in_top) hit = 7;    // Top-Right
    else if (in_left && in_bottom) hit = 8;  // Bottom-Left
    else if (in_right && in_bottom) hit = 9; // Bottom-Right
    else if (in_left && inside_y) hit = 1;   // Left
    else if (in_right && inside_y) hit = 2;  // Right
    else if (in_top && inside_x) hit = 3;    // Top
    else if (in_bottom && inside_x) hit = 4; // Bottom
    else if (inside_x && inside_y) hit = 5;  // Move Inside

    if (hit == 0) {
        move_commit(tool, doc);
        document_clear_selection(doc);
        return;
    }

    if (!md->active) {
        md->active = TRUE;
        md->hole_x = r.x; md->hole_y = r.y;
        md->hole_w = r.w; md->hole_h = r.h;
        
        int w = document_width(doc), h = document_height(doc);
        md->layer_w = w; md->layer_h = h;
        Layer *l = document_layer_at(doc, document_active_layer(doc));
        
        if (!md->backup_layer) md->backup_layer = pixel_buffer_new(w, h);
        memcpy(md->backup_layer->pixels, l->buffer->pixels, (gsize)w * h * sizeof(Pixel));

        md->floating = document_copy_region(doc);
        md->orig_floating = pixel_buffer_new(md->floating->width, md->floating->height);
        memcpy(md->orig_floating->pixels, md->floating->pixels, (gsize)md->floating->width * md->floating->height * sizeof(Pixel));
        
        Pixel tr = {0, 0, 0, 0};
        for (int y = r.y; y < r.y + r.h; y++) {
            for (int x = r.x; x < r.x + r.w; x++) {
                l->buffer->pixels[(gsize)y * w + x] = tr;
            }
        }
        document_begin_stroke(doc, "Transform Pixels");
    }

    md->drag_start_x = (int)(ev->x + 0.5);
    md->drag_start_y = (int)(ev->y + 0.5);
    md->sel_start_x = r.x;
    md->sel_start_y = r.y;
    md->sel_start_w = r.w;
    md->sel_start_h = r.h;
    
    md->dragging = TRUE;
    md->resize_edge = hit;
}

static void move_move(Tool *tool, Document *doc, InputEvent *ev) {
    MoveData *md = tool->user_data;
    if (!md->dragging || !md->active || !md->floating) return;

    int dx = (int)(ev->x + 0.5) - md->drag_start_x;
    int dy = (int)(ev->y + 0.5) - md->drag_start_y;
    int w = md->layer_w;
    Layer *l = document_layer_at(doc, document_active_layer(doc));
    
    // 1. Restore from backup
    memcpy(l->buffer->pixels, md->backup_layer->pixels, (gsize)w * md->layer_h * sizeof(Pixel));
    
    // 2. Punch hole at ORIGINAL location
    Pixel tr = {0, 0, 0, 0};
    for (int y = md->hole_y; y < md->hole_y + md->hole_h; y++) {
        for (int x = md->hole_x; x < md->hole_x + md->hole_w; x++) {
            if (x >= 0 && x < w && y >= 0 && y < md->layer_h)
                l->buffer->pixels[(gsize)y * w + x] = tr;
        }
    }

    int nx = md->sel_start_x;
    int ny = md->sel_start_y;
    int nw = md->sel_start_w;
    int nh = md->sel_start_h;

    if (md->resize_edge == 5) {
        // MOVE
        nx += dx;
        ny += dy;
        // If we were resizing previously, snap back to original size
        if (md->floating != md->orig_floating) {
            pixel_buffer_free(md->floating);
            md->floating = pixel_buffer_new(md->orig_floating->width, md->orig_floating->height);
            memcpy(md->floating->pixels, md->orig_floating->pixels, (gsize)md->orig_floating->width * md->orig_floating->height * sizeof(Pixel));
        }
        nw = md->floating->width;
        nh = md->floating->height;
    } else {
        // RESIZE
        if (md->resize_edge == 1 || md->resize_edge == 6 || md->resize_edge == 8) { nx += dx; nw -= dx; }
        if (md->resize_edge == 2 || md->resize_edge == 7 || md->resize_edge == 9) { nw += dx; }
        if (md->resize_edge == 3 || md->resize_edge == 6 || md->resize_edge == 7) { ny += dy; nh -= dy; }
        if (md->resize_edge == 4 || md->resize_edge == 8 || md->resize_edge == 9) { nh += dy; }

        if (nw < 4) nw = 4;
        if (nh < 4) nh = 4;
        
        PixelBuffer *scaled = scale_buffer_bilinear(md->orig_floating, nw, nh);
        pixel_buffer_free(md->floating);
        md->floating = scaled;
    }

    // 3. Paste floating at NEW location
    for (int by = 0; by < md->floating->height; by++) {
        int py = ny + by;
        if (py < 0 || py >= md->layer_h) continue;
        for (int bx = 0; bx < md->floating->width; bx++) {
            int px = nx + bx;
            if (px < 0 || px >= w) continue;
            l->buffer->pixels[(gsize)py * w + px] = md->floating->pixels[(gsize)by * md->floating->width + bx];
        }
    }
    
    document_set_selection(doc, nx, ny, nw, nh);
    document_invalidate(doc);
}

static void move_end(Tool *tool, Document *doc, InputEvent *ev) {
    (void)ev; (void)doc;
    MoveData *md = tool->user_data;
    md->dragging = FALSE;
}

static void move_commit(Tool *tool, Document *doc) {
    MoveData *md = tool->user_data;
    if (!md->active) return;
    if (md->floating) { pixel_buffer_free(md->floating); md->floating = NULL; }
    if (md->orig_floating) { pixel_buffer_free(md->orig_floating); md->orig_floating = NULL; }
    document_end_stroke(doc);
    md->active = FALSE;
}

static void move_deactivate(Tool *tool, Document *doc) { move_commit(tool, doc); }

static const ToolVTable move_vtable = {
    .id = "move", .name = "Transform", .icon_name = "transform-scale-symbolic",
    .activate = move_activate, .deactivate = move_deactivate,
    .stroke_begin = move_begin, .stroke_move = move_move, .stroke_end = move_end,
    .build_options_ui = NULL,
};

Tool *move_tool_new(void) {
    Tool *t = g_new0(Tool, 1);
    t->vtable = &move_vtable;
    t->user_data = g_new0(MoveData, 1);
    return t;
}
