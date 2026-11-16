#include "transform.h"
#include "../domain/compositor.h"
#include "../util/log.h"
#include <string.h>
#include <math.h>

static TransformContext g_ctx = {0};

TransformContext *transform_get(void) { return &g_ctx; }

static void transform_apply_rotation(TransformContext *ctx) {
    if (!ctx->base_floating) return;
    int w = ctx->base_w;
    int h = ctx->base_h;
    
    // Handle exact 0/90/180/270 for perfect pixel preservation
    int nw = w, nh = h;
    double rad = ctx->angle * G_PI / 180.0;
    double cos_a = cos(rad), sin_a = sin(rad);

    if (fabs(cos_a) < 0.001) { nw = h; nh = w; }
    else if (fabs(cos_a) < 0.999) {
        nw = (int)(fabs(w * cos_a) + fabs(h * sin_a) + 0.5);
        nh = (int)(fabs(w * sin_a) + fabs(h * cos_a) + 0.5);
    }

    if (ctx->floating) pixel_buffer_free(ctx->floating);
    ctx->floating = pixel_buffer_new(nw, nh);
    ctx->cur_w = nw;
    ctx->cur_h = nh;

    double cx = nw / 2.0, cy = nh / 2.0;
    double scx = w / 2.0, scy = h / 2.0;

    for (int y = 0; y < nh; y++) {
        for (int x = 0; x < nw; x++) {
            double dx = x - cx, dy = y - cy;
            double sx = dx * cos_a + dy * sin_a + scx;
            double sy = -dx * sin_a + dy * cos_a + scy;

            if (ctx->flip_h) sx = (w - 1) - sx;
            if (ctx->flip_v) sy = (h - 1) - sy;

            if (sx >= 0 && sy >= 0 && sx < w - 1 && sy < h - 1) {
                int x0 = (int)sx, y0 = (int)sy;
                float wx = sx - x0, wy = sy - y0;
                Pixel p00 = ctx->base_floating->pixels[y0 * w + x0];
                Pixel p10 = ctx->base_floating->pixels[y0 * w + x0 + 1];
                Pixel p01 = ctx->base_floating->pixels[(y0 + 1) * w + x0];
                Pixel p11 = ctx->base_floating->pixels[(y0 + 1) * w + x0 + 1];
                #define LERP(a, b, t) ((a) + ((b) - (a)) * (t))
                ctx->floating->pixels[y * nw + x].r = LERP(LERP(p00.r, p10.r, wx), LERP(p01.r, p11.r, wx), wy);
                ctx->floating->pixels[y * nw + x].g = LERP(LERP(p00.g, p10.g, wx), LERP(p01.g, p11.g, wx), wy);
                ctx->floating->pixels[y * nw + x].b = LERP(LERP(p00.b, p10.b, wx), LERP(p01.b, p11.b, wx), wy);
                ctx->floating->pixels[y * nw + x].a = LERP(LERP(p00.a, p10.a, wx), LERP(p01.a, p11.a, wx), wy);
            }
        }
    }
}

void transform_begin(Document *doc) {
    if (g_ctx.active) transform_commit(doc);
    if (!document_has_selection(doc)) return;

    SelectionRect r = document_selection(doc);
    g_ctx.active = TRUE;
    g_ctx.hole_x = r.x; g_ctx.hole_y = r.y;
    g_ctx.hole_w = r.w; g_ctx.hole_h = r.h;
    
    g_ctx.layer_w = document_width(doc);
    g_ctx.layer_h = document_height(doc);

    Layer *l = document_layer_at(doc, document_active_layer(doc));
    g_ctx.backup_layer = pixel_buffer_new(g_ctx.layer_w, g_ctx.layer_h);
    memcpy(g_ctx.backup_layer->pixels, l->buffer->pixels, (gsize)g_ctx.layer_w * g_ctx.layer_h * sizeof(Pixel));

    g_ctx.orig_floating = document_copy_region(doc);
    g_ctx.base_w = g_ctx.orig_floating->width;
    g_ctx.base_h = g_ctx.orig_floating->height;
    
    g_ctx.base_floating = pixel_buffer_new(g_ctx.base_w, g_ctx.base_h);
    memcpy(g_ctx.base_floating->pixels, g_ctx.orig_floating->pixels, (gsize)g_ctx.base_w * g_ctx.base_h * sizeof(Pixel));

    g_ctx.cur_w = g_ctx.base_w;
    g_ctx.cur_h = g_ctx.base_h;
    g_ctx.ref_center_x = r.x + r.w / 2.0;
    g_ctx.ref_center_y = r.y + r.h / 2.0;
    
    g_ctx.angle = 0;
    g_ctx.flip_h = FALSE;
    g_ctx.flip_v = FALSE;

    LOG_DEBUG("Transform begin: (%d,%d) %dx%d", r.x, r.y, r.w, r.h);
    document_begin_stroke(doc, "Transform Pixels");
    transform_update(doc);
}

void transform_update(Document *doc) {
    if (!g_ctx.active) return;
    if (g_ctx.layer_w != document_width(doc) || g_ctx.layer_h != document_height(doc)) {
        LOG_WARN("Canvas size changed mid-transform, aborting");
        transform_commit(doc);
        return;
    }

    transform_apply_rotation(&g_ctx);
    g_ctx.cur_x = (int)(g_ctx.ref_center_x - g_ctx.cur_w / 2.0);
    g_ctx.cur_y = (int)(g_ctx.ref_center_y - g_ctx.cur_h / 2.0);

    Layer *l = document_layer_at(doc, document_active_layer(doc));
    if (!l || !l->buffer) return;

    memcpy(l->buffer->pixels, g_ctx.backup_layer->pixels, (gsize)g_ctx.layer_w * g_ctx.layer_h * sizeof(Pixel));

    Pixel tr = {0, 0, 0, 0};
    for (int y = g_ctx.hole_y; y < g_ctx.hole_y + g_ctx.hole_h; y++) {
        for (int x = g_ctx.hole_x; x < g_ctx.hole_x + g_ctx.hole_w; x++) {
            if (x >= 0 && x < g_ctx.layer_w && y >= 0 && y < g_ctx.layer_h)
                l->buffer->pixels[(gsize)y * g_ctx.layer_w + x] = tr;
        }
    }

    if (g_ctx.floating) {
        for (int by = 0; by < g_ctx.floating->height; by++) {
            int py = g_ctx.cur_y + by;
            if (py < 0 || py >= g_ctx.layer_h) continue;
            for (int bx = 0; bx < g_ctx.floating->width; bx++) {
                int px = g_ctx.cur_x + bx;
                if (px < 0 || px >= g_ctx.layer_w) continue;
                compositor_blend_pixel(&l->buffer->pixels[(gsize)py * g_ctx.layer_w + px],
                                       &g_ctx.floating->pixels[(gsize)by * g_ctx.floating->width + bx],
                                       1.0f, BLEND_NORMAL);
            }
        }
    }

    document_set_selection(doc, g_ctx.cur_x, g_ctx.cur_y, g_ctx.cur_w, g_ctx.cur_h);
    document_invalidate(doc);
}

void transform_commit(Document *doc) {
    if (!g_ctx.active) return;
    if (doc) {
        transform_update(doc);
        document_end_stroke(doc);
    }
    
    if (g_ctx.backup_layer) { pixel_buffer_free(g_ctx.backup_layer); g_ctx.backup_layer = NULL; }
    if (g_ctx.orig_floating) { pixel_buffer_free(g_ctx.orig_floating); g_ctx.orig_floating = NULL; }
    if (g_ctx.base_floating) { pixel_buffer_free(g_ctx.base_floating); g_ctx.base_floating = NULL; }
    if (g_ctx.floating) { pixel_buffer_free(g_ctx.floating); g_ctx.floating = NULL; }
    LOG_DEBUG("Transform commit: %dx%d at (%d,%d)", g_ctx.cur_w, g_ctx.cur_h, g_ctx.cur_x, g_ctx.cur_y);
    g_ctx.active = FALSE;
}

void transform_reset(void) { transform_commit(NULL); }
