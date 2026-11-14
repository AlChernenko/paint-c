/* Sample plugin: Smooth Blur tool.
 * Build: cd addons/blur_tool && make install
 * Loaded by the host from ./build/addons/ at startup.
 */

#include "plugin/plugin_api.h"

typedef struct { int radius; } BlurData;

static void
blur_stroke (Tool *t, Document *doc, InputEvent *ev)
{
    BlurData *bd = t->user_data;
    int r = (int)(bd->radius * (ev->pressure > 0.01 ? ev->pressure : 1.0) + 0.5);
    if (r < 1) r = 1;
    int ix = (int)(ev->x + 0.5), iy = (int)(ev->y + 0.5);
    document_apply_blur (doc, ix, iy, r);
}

static const ToolVTable vtable = {
    .id = "com.example.blur", .name = "Smooth Blur", .icon_name = NULL,
    .activate = NULL, .deactivate = NULL,
    .stroke_begin = blur_stroke, .stroke_move = blur_stroke, .stroke_end = NULL,
    .build_options_ui = NULL,
};

static Tool *
make_tool (void)
{
    Tool *t = g_new0 (Tool, 1);
    t->vtable = &vtable;
    t->user_data = g_new0 (BlurData, 1);
    ((BlurData *)t->user_data)->radius = 6;
    return t;
}

static const PaintPluginDescriptor desc = {
    .api_version = PAINT_PLUGIN_ABI_VERSION,
    .id = "com.example.blur", .name = "Smooth Blur", .version = "0.1",
    .author = "paint examples",
    .create_tool = make_tool,
};

const PaintPluginDescriptor *
paint_plugin_get_descriptor (void) { return &desc; }
