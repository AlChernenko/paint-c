#include "blur.h"
#include "../input_event.h"

typedef struct { int radius; } BlurData;

static void blur_stroke(Tool *tool, Document *doc, InputEvent *ev) {
    BlurData *bd = tool->user_data;
    document_apply_blur(doc, (int)(ev->x + 0.5), (int)(ev->y + 0.5), bd->radius);
}

static const ToolVTable blur_vtable = {
    .id = "blur", 
    .name = "Blur", 
    .icon_name = "weather-fog-symbolic", 
    .group_id = "effects",
    .description = "Blur pixels (stub).",
    .stroke_begin = blur_stroke, 
    .stroke_move = blur_stroke, 
    .stroke_end = NULL,
    .build_options_ui = NULL,
};

Tool *blur_tool_new(void) {
    Tool *t = g_new0(Tool, 1);
    t->vtable = &blur_vtable;
    BlurData *bd = g_new0(BlurData, 1);
    bd->radius = 6;
    t->user_data = bd;
    return t;
}

void blur_tool_set_radius(Tool *t, int r) { ((BlurData *)t->user_data)->radius = MAX(1, r); }
int  blur_tool_get_radius(Tool *t)        { return ((BlurData *)t->user_data)->radius; }
