#include "select_rect.h"
#include "../input_event.h"
#include <string.h>

typedef enum { MODE_NONE, MODE_NEW, MODE_MOVE, MODE_RESIZE } SelMode;

typedef struct {
    SelMode mode;
    double click_x, click_y; // Where the mouse initially clicked
    int orig_x, orig_y, orig_w, orig_h; // Original selection rect
    int resize_edge; // 1=left, 2=right, 3=top, 4=bottom
} SelectData;

static int hit_test(Document *doc, double x, double y) {
    if (!document_has_selection(doc)) return 0;
    SelectionRect r = document_selection(doc);
    int margin = 8;
    int inside_x = (x >= r.x && x <= r.x + r.w);
    int inside_y = (y >= r.y && y <= r.y + r.h);
    
    if (inside_y && x > r.x - margin && x < r.x + margin) return 1;
    if (inside_y && x > r.x + r.w - margin && x < r.x + r.w + margin) return 2;
    if (inside_x && y > r.y - margin && y < r.y + margin) return 3;
    if (inside_x && y > r.y + r.h - margin && y < r.y + r.h + margin) return 4;
    if (inside_x && inside_y) return 5; // inside -> move
    return 0;
}

static void select_begin(Tool *tool, Document *doc, InputEvent *ev) {
    SelectData *sd = tool->user_data;
    sd->click_x = ev->x;
    sd->click_y = ev->y;
    
    int hit = hit_test(doc, ev->x, ev->y);
    if (hit >= 1 && hit <= 4) {
        sd->mode = MODE_RESIZE;
        sd->resize_edge = hit;
        SelectionRect r = document_selection(doc);
        sd->orig_x = r.x; sd->orig_y = r.y; sd->orig_w = r.w; sd->orig_h = r.h;
    } else if (hit == 5) {
        sd->mode = MODE_MOVE;
        SelectionRect r = document_selection(doc);
        sd->orig_x = r.x; sd->orig_y = r.y; sd->orig_w = r.w; sd->orig_h = r.h;
    } else {
        sd->mode = MODE_NEW;
        document_clear_selection(doc);
    }
}

static void select_move(Tool *tool, Document *doc, InputEvent *ev) {
    SelectData *sd = tool->user_data;
    if (sd->mode == MODE_NONE) return;

    // Calculate relative movement from where we clicked
    int dx = (int)(ev->x - sd->click_x + 0.5);
    int dy = (int)(ev->y - sd->click_y + 0.5);

    if (sd->mode == MODE_NEW) {
        int x = (int)sd->click_x;
        int y = (int)sd->click_y;
        int w = (int)(ev->x - sd->click_x + 0.5);
        int h = (int)(ev->y - sd->click_y + 0.5);
        if (w < 0) { x += w; w = -w; }
        if (h < 0) { y += h; h = -h; }
        document_set_selection(doc, x, y, w, h);
    } else if (sd->mode == MODE_MOVE) {
        // Move the ORIGINAL rect by the delta
        document_set_selection(doc, sd->orig_x + dx, sd->orig_y + dy, sd->orig_w, sd->orig_h);
    } else if (sd->mode == MODE_RESIZE) {
        int x = sd->orig_x, y = sd->orig_y, w = sd->orig_w, h = sd->orig_h;
        if (sd->resize_edge == 1) { x += dx; w -= dx; }
        if (sd->resize_edge == 2) { w += dx; }
        if (sd->resize_edge == 3) { y += dy; h -= dy; }
        if (sd->resize_edge == 4) { h += dy; }
        if (w < 2) w = 2;
        if (h < 2) h = 2;
        document_set_selection(doc, x, y, w, h);
    }
}

static void select_end(Tool *tool, Document *doc, InputEvent *ev) {
    (void)doc; (void)ev;
    ((SelectData *)tool->user_data)->mode = MODE_NONE;
}

static const ToolVTable select_vtable = {
    .id = "select",
    .group_id = "transform", .name = "Select",
    .description = "Select a rectangular region.", .icon_name = "edit-select-all-symbolic",
    
    
    .activate = NULL, .deactivate = NULL,
    .stroke_begin = select_begin, .stroke_move = select_move, .stroke_end = select_end,
    .build_options_ui = NULL,
};

Tool *select_rect_tool_new(void) {
    Tool *t = g_new0(Tool, 1);
    t->vtable = &select_vtable;
    t->user_data = g_new0(SelectData, 1);
    return t;
}
