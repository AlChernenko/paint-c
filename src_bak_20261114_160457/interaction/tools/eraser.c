#include "eraser.h"

typedef struct { int radius; } EraserData;

static int
pressure_radius (EraserData *ed, InputEvent *ev)
{
    double p = ev->pressure > 0.01 ? ev->pressure : 1.0;
    int r = (int)(ed->radius * p);
    return MAX (r, 1);
}

static void
eraser_stroke (Tool *tool, Document *doc, InputEvent *ev)
{
    EraserData *ed = tool->user_data;
    document_apply_erase (doc, (int)ev->x, (int)ev->y,
                          pressure_radius (ed, ev));
}

static const ToolVTable eraser_vtable = {
    .id               = "eraser",
    .name             = "Eraser",
    .icon_name        = "paint-eraser-symbolic",
    .activate         = NULL,
    .deactivate       = NULL,
    .stroke_begin     = eraser_stroke,
    .stroke_move      = eraser_stroke,
    .stroke_end       = NULL,
    .build_options_ui = NULL,
};

Tool *
eraser_tool_new (void)
{
    Tool *t = g_new0 (Tool, 1);
    t->vtable = &eraser_vtable;
    EraserData *ed = g_new0 (EraserData, 1);
    ed->radius = 8;
    t->user_data = ed;
    return t;
}

void
eraser_tool_set_radius (Tool *tool, int radius)
{
    ((EraserData *)tool->user_data)->radius = MAX (1, radius);
}

int
eraser_tool_get_radius (Tool *tool)
{
    return ((EraserData *)tool->user_data)->radius;
}
