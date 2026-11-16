#include "move.h"
#include "../input_event.h"
#include "../transform.h"
#include <string.h>

typedef struct {
    int drag_start_x, drag_start_y;
    int sel_start_x, sel_start_y;
    int resize_edge;
    gboolean dragging;
    int orig_base_w, orig_base_h;
    double orig_center_x, orig_center_y;
} MoveData;

static PixelBuffer *scale_buffer_bilinear(const PixelBuffer *src, int nw, int nh) {
    PixelBuffer *dst = pixel_buffer_new(nw, nh);
    if (!src || nw <= 0 || nh <= 0 || src->width == 0 || src->height == 0) return dst;
    for (int y = 0; y < nh; y++) {
        for (int x = 0; x < nw; x++) {
            float gx = (nw > 1) ? (float)x / (nw - 1) * (src->width - 1) : 0;
            float gy = (nh > 1) ? (float)y / (nh - 1) * (src->height - 1) : 0;
            int x0 = (int)gx, y0 = (int)gy, x1 = x0 + 1, y1 = y0 + 1;
            if (x1 >= src->width) x1 = src->width - 1;
            if (y1 >= src->height) y1 = src->height - 1;
            float wx = gx - x0, wy = gy - y0;
            Pixel p00 = src->pixels[y0 * src->width + x0], p10 = src->pixels[y0 * src->width + x1];
            Pixel p01 = src->pixels[y1 * src->width + x0], p11 = src->pixels[y1 * src->width + x1];
            #define LERP(a, b, t) ((a) + ((b) - (a)) * (t))
            dst->pixels[y * nw + x].r = LERP(LERP(p00.r, p10.r, wx), LERP(p01.r, p11.r, wx), wy);
            dst->pixels[y * nw + x].g = LERP(LERP(p00.g, p10.g, wx), LERP(p01.g, p11.g, wx), wy);
            dst->pixels[y * nw + x].b = LERP(LERP(p00.b, p10.b, wx), LERP(p01.b, p11.b, wx), wy);
            dst->pixels[y * nw + x].a = LERP(LERP(p00.a, p10.a, wx), LERP(p01.a, p11.a, wx), wy);
        }
    }
    return dst;
}

static void move_begin(Tool *tool, Document *doc, InputEvent *ev) {
    MoveData *md = tool->user_data;
    if (!document_has_selection(doc)) return;

    TransformContext *ctx = transform_get();
    if (!ctx->active) {
        transform_begin(doc);
        if (!ctx->active) return;
    }

    SelectionRect r = document_selection(doc);
    int margin = 8;
    int in_left = (ev->x > r.x - margin && ev->x < r.x + margin);
    int in_right = (ev->x > r.x + r.w - margin && ev->x < r.x + r.w + margin);
    int in_top = (ev->y > r.y - margin && ev->y < r.y + margin);
    int in_bottom = (ev->y > r.y + r.h - margin && ev->y < r.y + r.h + margin);
    int inside_x = (ev->x >= r.x && ev->x <= r.x + r.w);
    int inside_y = (ev->y >= r.y && ev->y <= r.y + r.h);

    int hit = 0;
    if (in_left && in_top) hit = 6; else if (in_right && in_top) hit = 7;
    else if (in_left && in_bottom) hit = 8; else if (in_right && in_bottom) hit = 9;
    else if (in_left && inside_y) hit = 1; else if (in_right && inside_y) hit = 2;
    else if (in_top && inside_x) hit = 3; else if (in_bottom && inside_x) hit = 4;
    else if (inside_x && inside_y) hit = 5;

    if (hit == 0) {
        transform_commit(doc);
        document_clear_selection(doc);
        return;
    }

    md->drag_start_x = (int)(ev->x + 0.5);
    md->drag_start_y = (int)(ev->y + 0.5);
    md->sel_start_x = r.x; md->sel_start_y = r.y;
    md->orig_base_w = ctx->base_w;
    md->orig_base_h = ctx->base_h;
    md->orig_center_x = ctx->ref_center_x;
    md->orig_center_y = ctx->ref_center_y;
    
    md->dragging = TRUE;
    md->resize_edge = hit;
}

static void move_move(Tool *tool, Document *doc, InputEvent *ev) {
    MoveData *md = tool->user_data;
    if (!md->dragging) return;
    TransformContext *ctx = transform_get();
    if (!ctx->active) return;

    int dx = (int)(ev->x + 0.5) - md->drag_start_x;
    int dy = (int)(ev->y + 0.5) - md->drag_start_y;

    if (md->resize_edge == 5) {
        ctx->ref_center_x = md->orig_center_x + dx;
        ctx->ref_center_y = md->orig_center_y + dy;
    } else {
        int nw = md->orig_base_w;
        int nh = md->orig_base_h;
        double cx = md->orig_center_x;
        double cy = md->orig_center_y;

        if (md->resize_edge == 1 || md->resize_edge == 6 || md->resize_edge == 8) { nw -= dx; cx += dx / 2.0; }
        if (md->resize_edge == 2 || md->resize_edge == 7 || md->resize_edge == 9) { nw += dx; cx += dx / 2.0; }
        if (md->resize_edge == 3 || md->resize_edge == 6 || md->resize_edge == 7) { nh -= dy; cy += dy / 2.0; }
        if (md->resize_edge == 4 || md->resize_edge == 8 || md->resize_edge == 9) { nh += dy; cy += dy / 2.0; }

        if (nw < 4) nw = 4;
        if (nh < 4) nh = 4;
        
        ctx->base_w = nw;
        ctx->base_h = nh;
        ctx->ref_center_x = cx;
        ctx->ref_center_y = cy;

        if (ctx->base_floating) pixel_buffer_free(ctx->base_floating);
        ctx->base_floating = scale_buffer_bilinear(ctx->orig_floating, nw, nh);
    }

    transform_update(doc);
}

static void move_end(Tool *tool, Document *doc, InputEvent *ev) {
    (void)ev; (void)doc;
    ((MoveData *)tool->user_data)->dragging = FALSE;
}

static const ToolVTable move_vtable = {
    .id = "move", .name = "Transform", .icon_name = "transform-scale-symbolic",
    .group_id = "transform",
    .description = "Move and resize selected pixels.",
    .stroke_begin = move_begin, .stroke_move = move_move, .stroke_end = move_end,
};

Tool *move_tool_new(void) {
    Tool *t = g_new0(Tool, 1);
    t->vtable = &move_vtable;
    t->user_data = g_new0(MoveData, 1);
    return t;
}
