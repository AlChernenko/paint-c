# Paint Addons

Each subdirectory is one addon. Addons are compiled to shared libraries
(`.so` on Linux, `.dll` on Windows) and loaded at runtime by the host.

## Writing an addon

1. Create `addons/my_tool/my_tool.c`.
2. Include `<paint/plugin_api.h>` (the include path points to `src/`).
3. Implement your `ToolVTable`.
4. Export `paint_plugin_get_descriptor` returning a `PaintPluginDescriptor`.
5. Build it (see `blur_tool/Makefile`).
6. Drop the resulting `.so` in `build/addons/` and restart Paint.

## Minimum example

```c
#include "paint/plugin_api.h"

static void
my_stroke (Tool *t, Document *doc, InputEvent *ev)
{
    document_apply_dot (doc, (int)ev->x, (int)ev->y, 3);
}

static const ToolVTable vt = {
    .id = "com.example.my", .name = "My Tool",
    .stroke_begin = my_stroke, .stroke_move = my_stroke,
};

static Tool *
make_tool (void)
{
    Tool *t = g_new0 (Tool, 1);
    t->vtable = &vt;
    return t;
}

static const PaintPluginDescriptor desc = {
    .api_version = PAINT_PLUGIN_ABI_VERSION,
    .id = "com.example.my", .name = "My Tool", .version = "0.1",
    .author = "you",
    .create_tool = make_tool,
};

const PaintPluginDescriptor *
paint_plugin_get_descriptor (void) { return &desc; }

### 4g. `addons/blur_tool/blur_tool.c` — sample addon

```bash
cat > addons/blur_tool/blur_tool.c << 'EOF'
#include "paint/plugin_api.h"

typedef struct { int radius; } BlurData;

static void
blur_stroke (Tool *t, Document *doc, InputEvent *ev)
{
    BlurData *bd = t->user_data;
    int r = (int)(bd->radius * (ev->pressure > 0.01 ? ev->pressure : 1.0));
    if (r < 1) r = 1;
    document_apply_blur (doc, (int)ev->x, (int)ev->y, r);
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
