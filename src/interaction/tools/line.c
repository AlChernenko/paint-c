#include "line.h"
#include "../input_event.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    int start_x, start_y;
    Pixel color;
    int radius;
    PixelBuffer *original_layer;
} LineData;


static gboolean in_sel(Document *doc, int x, int y) {
    if (!document_has_selection(doc)) return TRUE;
    SelectionRect r = document_selection(doc);
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

static void line_begin(Tool *tool, Document *doc, InputEvent *ev) {
    LineData *ld = tool->user_data;
    ld->start_x = (int)(ev->x + 0.5);
    ld->start_y = (int)(ev->y + 0.5);
    
    // Save original layer for preview
    int w = document_width(doc), h = document_height(doc);
    Layer *l = document_layer_at(doc, document_active_layer(doc));
    if (!ld->original_layer) ld->original_layer = pixel_buffer_new(w, h);
    memcpy(ld->original_layer->pixels, l->buffer->pixels, (gsize)w*h*sizeof(Pixel));
}

static void line_move(Tool *tool, Document *doc, InputEvent *ev) {
    LineData *ld = tool->user_data;
    int x1 = (int)(ev->x + 0.5), y1 = (int)(ev->y + 0.5);
    
    // Restore original
    int w = document_width(doc), h = document_height(doc);
    Layer *l = document_layer_at(doc, document_active_layer(doc));
    memcpy(l->buffer->pixels, ld->original_layer->pixels, (gsize)w*h*sizeof(Pixel));
    
    // Draw new preview line (bypassing document_apply so it doesn't spam history)
    int dx = abs(x1 - ld->start_x), sx = ld->start_x < x1 ? 1 : -1;
    int dy = -abs(y1 - ld->start_y), sy = ld->start_y < y1 ? 1 : -1;
    int err = dx + dy, cx = ld->start_x, cy = ld->start_y;
    while (1) {
        if (cx >= 0 && cx < w && cy >= 0 && cy < h && in_sel(doc, cx, cy)) {
            l->buffer->pixels[(gsize)cy * w + cx] = ld->color;
        }
        if (cx == x1 && cy == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
    document_invalidate(doc);
}

static void line_end(Tool *tool, Document *doc, InputEvent *ev) {
    line_move(tool, doc, ev); // final draw
    pixel_buffer_free(((LineData *)tool->user_data)->original_layer);
    ((LineData *)tool->user_data)->original_layer = NULL;
}

static const ToolVTable line_vtable = {
    .id = "line",
    .group_id = "misc", .name = "Line",
    .description = "Draw straight lines.", .icon_name = "insert-object-symbolic",
    
    
    .activate = NULL, .deactivate = NULL,
    .stroke_begin = line_begin, .stroke_move = line_move, .stroke_end = line_end,
    .build_options_ui = NULL,
};

Tool *line_tool_new(void) {
    Tool *t = g_new0(Tool, 1);
    t->vtable = &line_vtable;
    t->user_data = g_new0(LineData, 1);
    ((LineData *)t->user_data)->color = (Pixel){0, 0, 0, 255};
    ((LineData *)t->user_data)->radius = 2;
    return t;
}
