#include "pencil.h"

typedef struct {
    Pixel color;
    int radius;
    double last_x, last_y;
    gboolean has_last;
} PencilData;

static int
press_radius (PencilData *pd, InputEvent *ev)
{
    int r = (int)(pd->radius * (ev->pressure > 0.01 ? ev->pressure : 1.0) + 0.5);
    return MAX (r, 1);
}

static void
pencil_stroke_begin (Tool *tool, Document *doc, InputEvent *ev)
{
    PencilData *pd = tool->user_data;
    int r = press_radius (pd, ev);
    int ix = (int)(ev->x + 0.5), iy = (int)(ev->y + 0.5);
    document_apply_dot (doc, ix, iy, pd->color, r);
    pd->last_x = ev->x; pd->last_y = ev->y;
    pd->has_last = TRUE;
}

static void
pencil_stroke_move (Tool *tool, Document *doc, InputEvent *ev)
{
    PencilData *pd = tool->user_data;
    int r = press_radius (pd, ev);
    int x1 = (int)(ev->x + 0.5), y1 = (int)(ev->y + 0.5);
    if (pd->has_last) {
        int x0 = (int)(pd->last_x + 0.5), y0 = (int)(pd->last_y + 0.5);
        document_apply_stroke (doc, x0, y0, x1, y1, pd->color, r);
    } else {
        document_apply_dot (doc, x1, y1, pd->color, r);
    }
    pd->last_x = ev->x; pd->last_y = ev->y;
    pd->has_last = TRUE;
}

static void
pencil_stroke_end (Tool *tool, Document *doc, InputEvent *ev)
{
    (void)doc; (void)ev;
    ((PencilData *)tool->user_data)->has_last = FALSE;
}

static const ToolVTable pencil_vtable = {
    .id = "pencil",
    .group_id = "brush", .name = "Pencil",
    .description = "Draw hard, 1px lines.", .icon_name = NULL,
    .activate = NULL, .deactivate = NULL,
    .stroke_begin = pencil_stroke_begin,
    .stroke_move  = pencil_stroke_move,
    .stroke_end   = pencil_stroke_end,
    .build_options_ui = NULL,
};

Tool *pencil_tool_new (void)
{
    Tool *t = g_new0 (Tool, 1);
    t->vtable = &pencil_vtable;
    PencilData *pd = g_new0 (PencilData, 1);
    pd->color = (Pixel){0,0,0,255};
    pd->radius = 2;
    t->user_data = pd;
    return t;
}

void pencil_tool_set_color  (Tool *t, Pixel c) { ((PencilData *)t->user_data)->color  = c; }
void pencil_tool_set_radius (Tool *t, int r)   { ((PencilData *)t->user_data)->radius = MAX (1, r); }
int  pencil_tool_get_radius (Tool *t)          { return ((PencilData *)t->user_data)->radius; }
