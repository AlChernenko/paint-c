#include "brush.h"
#include "../input_event.h"
#include <stdlib.h>

typedef struct {
    Pixel color;
    int radius;
    float hardness;
    float opacity;
    double last_x, last_y;
    gboolean has_last;
} BrushData;

static void brush_draw(BrushData *bd, Document *doc, int x, int y) {
    document_apply_brush(doc, x, y, bd->radius, bd->hardness, bd->opacity, bd->color);
}

static void brush_begin(Tool *tool, Document *doc, InputEvent *ev) {
    BrushData *bd = tool->user_data;
    bd->last_x = ev->x; bd->last_y = ev->y; bd->has_last = TRUE;
    brush_draw(bd, doc, (int)(ev->x + 0.5), (int)(ev->y + 0.5));
}

static void brush_move(Tool *tool, Document *doc, InputEvent *ev) {
    BrushData *bd = tool->user_data;
    int x0 = (int)(bd->last_x + 0.5), y0 = (int)(bd->last_y + 0.5);
    int x1 = (int)(ev->x + 0.5), y1 = (int)(ev->y + 0.5);
    
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, cx = x0, cy = y0;
    
    while (1) {
        brush_draw(bd, doc, cx, cy);
        if (cx == x1 && cy == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
    bd->last_x = ev->x; bd->last_y = ev->y;
}

static void brush_end(Tool *tool, Document *doc, InputEvent *ev) {
    (void)doc; (void)ev;
    ((BrushData *)tool->user_data)->has_last = FALSE;
}

/* Dynamic UI for the top bar */
static void on_size_changed(GtkSpinButton *spin, gpointer ud) { ((BrushData *)ud)->radius = gtk_spin_button_get_value_as_int(spin); }
static void on_hardness_changed(GtkRange *range, gpointer ud) { ((BrushData *)ud)->hardness = gtk_range_get_value(range); }
static void on_opacity_changed(GtkRange *range, gpointer ud) { ((BrushData *)ud)->opacity = gtk_range_get_value(range); }

static GtkWidget *brush_build_options(Tool *tool) {
    BrushData *bd = tool->user_data;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget *r1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(r1), gtk_label_new("Size:"));
    GtkWidget *spin = gtk_spin_button_new_with_range(1, 200, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), bd->radius);
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_size_changed), bd);
    gtk_widget_set_hexpand(spin, TRUE);
    gtk_box_append(GTK_BOX(r1), spin);
    gtk_box_append(GTK_BOX(box), r1);

    GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(r2), gtk_label_new("Hardness:"));
    GtkWidget *hard = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.05);
    gtk_widget_set_hexpand(hard, TRUE);
    gtk_range_set_value(GTK_RANGE(hard), bd->hardness);
    g_signal_connect(hard, "value-changed", G_CALLBACK(on_hardness_changed), bd);
    gtk_box_append(GTK_BOX(r2), hard);
    gtk_box_append(GTK_BOX(box), r2);

    GtkWidget *r3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(r3), gtk_label_new("Opacity:"));
    GtkWidget *op = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.05);
    gtk_widget_set_hexpand(op, TRUE);
    gtk_range_set_value(GTK_RANGE(op), bd->opacity);
    g_signal_connect(op, "value-changed", G_CALLBACK(on_opacity_changed), bd);
    gtk_box_append(GTK_BOX(r3), op);
    gtk_box_append(GTK_BOX(box), r3);

    return box;
}

static const ToolVTable brush_vtable = {
    .id = "brush",
    .group_id = "brush", .name = "Brush",
    .description = "Soft brush with pressure and opacity.", .icon_name = "draw-brush-symbolic",
     
    .activate = NULL, .deactivate = NULL,
    .stroke_begin = brush_begin, .stroke_move = brush_move, .stroke_end = brush_end,
    .build_options_ui = brush_build_options,
};

Tool *brush_tool_new(void) {
    Tool *t = g_new0(Tool, 1);
    t->vtable = &brush_vtable;
    BrushData *bd = g_new0(BrushData, 1);
    bd->color = (Pixel){0, 0, 0, 255};
    bd->radius = 15;
    bd->hardness = 0.5f;
    bd->opacity = 1.0f;
    t->user_data = bd;
    return t;
}
