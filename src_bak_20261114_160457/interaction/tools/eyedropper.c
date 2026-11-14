#include "eyedropper.h"

typedef struct {
    Pixel color;
    EyedropperPickFn on_pick;
    void *on_pick_data;
} EyedropperData;

static void
eyedropper_pick (Tool *tool, Document *doc, InputEvent *ev)
{
    EyedropperData *ed = tool->user_data;
    int w = document_width (doc);
    int h = document_height (doc);
    int x = (int)ev->x;
    int y = (int)ev->y;
    if (x < 0 || y < 0 || x >= w || y >= h) return;

    const uint8_t *composite = document_composite (doc);
    const Pixel *p = (const Pixel *)(composite + ((gsize)y * w + x) * 4);
    ed->color = *p;
    if (ed->on_pick) ed->on_pick (*p, ed->on_pick_data);
}

static const ToolVTable eyedropper_vtable = {
    .id = "eyedropper", .name = "Eyedropper", .icon_name = NULL,
    .activate = NULL, .deactivate = NULL,
    .stroke_begin = eyedropper_pick, .stroke_move = eyedropper_pick, .stroke_end = NULL,
    .build_options_ui = NULL,
};

Tool *
eyedropper_tool_new (void)
{
    Tool *t = g_new0 (Tool, 1);
    t->vtable = &eyedropper_vtable;
    t->user_data = g_new0 (EyedropperData, 1);
    return t;
}

void
eyedropper_tool_set_callback (Tool *t, EyedropperPickFn fn, void *ud)
{
    EyedropperData *ed = t->user_data;
    ed->on_pick = fn;
    ed->on_pick_data = ud;
}
