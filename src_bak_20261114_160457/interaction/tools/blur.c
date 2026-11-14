#include "blur.h"

typedef struct {
    int radius;
    double last_x, last_y;
    gboolean has_last;
} BlurData;

static int
press_radius (BlurData *bd, InputEvent *ev)
{
    int r = (int)(bd->radius * (ev->pressure > 0.01 ? ev->pressure : 1.0) + 0.5);
    return MAX (r, 1);
}

static void
blur_begin (Tool *tool, Document *doc, InputEvent *ev)
{
    BlurData *bd = tool->user_data;
    int r = press_radius (bd, ev);
    int ix = (int)(ev->x + 0.5), iy = (int)(ev->y + 0.5);
    document_apply_blur (doc, ix, iy, r);
    bd->last_x = ev->x; bd->last_y = ev->y; bd->has_last = TRUE;
}

static void
blur_move (Tool *tool, Document *doc, InputEvent *ev)
{
    BlurData *bd = tool->user_data;
    int r = press_radius (bd, ev);
    int x1 = (int)(ev->x + 0.5), y1 = (int)(ev->y + 0.5);
    if (bd->has_last) {
        int x0 = (int)(bd->last_x + 0.5), y0 = (int)(bd->last_y + 0.5);
        document_apply_blur_stroke (doc, x0, y0, x1, y1, r);
    } else {
        document_apply_blur (doc, x1, y1, r);
    }
    bd->last_x = ev->x; bd->last_y = ev->y; bd->has_last = TRUE;
}

static void
blur_end (Tool *tool, Document *doc, InputEvent *ev)
{
    (void)doc; (void)ev;
    ((BlurData *)tool->user_data)->has_last = FALSE;
}

static const ToolVTable blur_vtable = {
    .id = "blur", .name = "Blur", .icon_name = NULL,
    .activate = NULL, .deactivate = NULL,
    .stroke_begin = blur_begin, .stroke_move = blur_move, .stroke_end = blur_end,
    .build_options_ui = NULL,
};

Tool *blur_tool_new (void)
{
    Tool *t = g_new0 (Tool, 1);
    t->vtable = &blur_vtable;
    BlurData *bd = g_new0 (BlurData, 1);
    bd->radius = 6;
    t->user_data = bd;
    return t;
}

void blur_tool_set_radius (Tool *t, int r) { ((BlurData *)t->user_data)->radius = MAX (1, r); }
int  blur_tool_get_radius (Tool *t)        { return ((BlurData *)t->user_data)->radius; }
