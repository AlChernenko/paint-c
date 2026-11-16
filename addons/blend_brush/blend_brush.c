#include "plugin/plugin_api.h"
#include <stdlib.h>

static int g_blend_mode = 0;
static int g_radius = 15;
static float g_hardness = 0.5f;

typedef struct {
    double last_x, last_y;
} BlendBrushData;

static void blend_draw_at(BlendBrushData *bd, Document *doc, int x, int y) {
    Pixel color = paint_plugin_get_primary_color(); // Fetch from app!
    document_apply_brush_blend(doc, x, y, g_radius, g_hardness, color, g_blend_mode);
}

static void blend_brush_begin(Tool *t, Document *doc, InputEvent *ev) {
    BlendBrushData *bd = t->user_data;
    bd->last_x = ev->x; bd->last_y = ev->y;
    blend_draw_at(bd, doc, (int)(ev->x + 0.5), (int)(ev->y + 0.5));
}

static void blend_brush_move(Tool *t, Document *doc, InputEvent *ev) {
    BlendBrushData *bd = t->user_data;
    int x0 = (int)(bd->last_x + 0.5), y0 = (int)(bd->last_y + 0.5);
    int x1 = (int)(ev->x + 0.5), y1 = (int)(ev->y + 0.5);
    
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, cx = x0, cy = y0;
    
    while (1) {
        blend_draw_at(bd, doc, cx, cy);
        if (cx == x1 && cy == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
    bd->last_x = ev->x; bd->last_y = ev->y;
}

static void on_mode_changed(GtkDropDown *dd, GParamSpec *pspec, gpointer ud) {
    (void)pspec; (void)ud;
    g_blend_mode = gtk_drop_down_get_selected(dd);
    char buf[32]; snprintf(buf, sizeof buf, "%d", g_blend_mode);
    paint_plugin_set_setting("blend_brush/mode", buf);
}

static void on_size_changed(GtkSpinButton *spin, gpointer ud) {
    (void)ud;
    g_radius = gtk_spin_button_get_value_as_int(spin);
}

static void on_hardness_changed(GtkRange *range, gpointer ud) {
    (void)ud;
    g_hardness = gtk_range_get_value(range);
}

static GtkWidget *build_blend_options(Tool *tool) {
    (void)tool;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    
    GtkWidget *s_lbl = gtk_label_new("Size:"); gtk_widget_set_halign(s_lbl, GTK_ALIGN_START);
    GtkWidget *s_spin = gtk_spin_button_new_with_range(1, 200, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(s_spin), g_radius);
    g_signal_connect(s_spin, "value-changed", G_CALLBACK(on_size_changed), NULL);

    GtkWidget *h_lbl = gtk_label_new("Hardness:"); gtk_widget_set_halign(h_lbl, GTK_ALIGN_START);
    GtkWidget *h_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.05);
    gtk_range_set_value(GTK_RANGE(h_scale), g_hardness);
    g_signal_connect(h_scale, "value-changed", G_CALLBACK(on_hardness_changed), NULL);

    GtkWidget *m_lbl = gtk_label_new("Blend Mode:"); gtk_widget_set_halign(m_lbl, GTK_ALIGN_START);
    const char *modes[] = { "Normal", "Multiply", "Screen", "Overlay", "Difference", "Addition", NULL };
    GtkStringList *model = gtk_string_list_new(modes);
    GtkWidget *dd = gtk_drop_down_new(G_LIST_MODEL(model), NULL);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), g_blend_mode);
    g_signal_connect(dd, "notify::selected", G_CALLBACK(on_mode_changed), NULL);

    gtk_box_append(GTK_BOX(box), s_lbl); gtk_box_append(GTK_BOX(box), s_spin);
    gtk_box_append(GTK_BOX(box), h_lbl); gtk_box_append(GTK_BOX(box), h_scale);
    gtk_box_append(GTK_BOX(box), m_lbl); gtk_box_append(GTK_BOX(box), dd);
    return box;
}

static const ToolVTable blend_brush_vtable = {
    .id = "blend_brush", .name = "Blend Brush", .icon_name = "applications-graphics-symbolic", .group_id = "effects",
    .description = "Uses app color with blend modes.",
    .stroke_begin = blend_brush_begin, .stroke_move = blend_brush_move, .stroke_end = NULL,
    .build_options_ui = build_blend_options,
};

static Tool *create_tool(void) {
    Tool *t = g_new0(Tool, 1);
    t->vtable = &blend_brush_vtable;
    t->user_data = g_new0(BlendBrushData, 1);
    
    // Load saved setting
    char buf[32];
    paint_plugin_get_setting("blend_brush/mode", buf, sizeof buf);
    if (buf[0]) g_blend_mode = atoi(buf);
    
    return t;
}

static const PaintPluginDescriptor desc = {
    .api_version = PAINT_PLUGIN_ABI_VERSION,
    .id = "com.example.blend_brush", .name = "Blend Brush", .version = "1.3",
    .author = "paint examples", .group_id = "effects",
    .create_tool = create_tool, .build_settings_ui = NULL,
};

const PaintPluginDescriptor *paint_plugin_get_descriptor(void) { return &desc; }
