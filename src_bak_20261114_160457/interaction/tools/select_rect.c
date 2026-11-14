#include "select_rect.h"

typedef struct {
    double start_x, start_y;
    gboolean dragging;
} SelectData;

static void
update_sel (Document *doc, SelectData *sd, InputEvent *ev)
{
    int x0 = (int)(sd->start_x + 0.5), y0 = (int)(sd->start_y + 0.5);
    int x1 = (int)(ev->x + 0.5),       y1 = (int)(ev->y + 0.5);
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    int w = x1 - x0, h = y1 - y0;
    if (w < 2 || h < 2) document_clear_selection (doc);
    else                document_set_selection (doc, x0, y0, w, h);
}

static void
select_begin (Tool *tool, Document *doc, InputEvent *ev)
{
    SelectData *sd = tool->user_data;
    sd->start_x = ev->x;
    sd->start_y = ev->y;
    sd->dragging = TRUE;
    update_sel (doc, sd, ev);
}

static void
select_move (Tool *tool, Document *doc, InputEvent *ev)
{
    SelectData *sd = tool->user_data;
    if (!sd->dragging) return;
    update_sel (doc, sd, ev);
}

static void
select_end (Tool *tool, Document *doc, InputEvent *ev)
{
    SelectData *sd = tool->user_data;
    if (!sd->dragging) return;
    sd->dragging = FALSE;
    update_sel (doc, sd, ev);
}

static const ToolVTable select_vtable = {
    .id = "select", .name = "Select", .icon_name = NULL,
    .activate = NULL, .deactivate = NULL,
    .stroke_begin = select_begin,
    .stroke_move  = select_move,
    .stroke_end   = select_end,
    .build_options_ui = NULL,
};

Tool *
select_rect_tool_new (void)
{
    Tool *t = g_new0 (Tool, 1);
    t->vtable = &select_vtable;
    t->user_data = g_new0 (SelectData, 1);
    return t;
}
