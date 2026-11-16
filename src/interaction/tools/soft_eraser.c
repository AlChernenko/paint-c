#include "soft_eraser.h"
#include "../input_event.h"
#include <stdlib.h>

typedef struct {
    int radius;
    float hardness;
    float opacity;
    double last_x, last_y;
} SoftEraserData;

static void erase_draw(SoftEraserData *ed, Document *doc, int x, int y) {
    document_apply_soft_erase(doc, x, y, ed->radius, ed->hardness, ed->opacity);
}

static void soft_eraser_begin(Tool *tool, Document *doc, InputEvent *ev) {
    SoftEraserData *ed = tool->user_data;
    ed->last_x = ev->x; ed->last_y = ev->y;
    erase_draw(ed, doc, (int)(ev->x + 0.5), (int)(ev->y + 0.5));
}

static void soft_eraser_move(Tool *tool, Document *doc, InputEvent *ev) {
    SoftEraserData *ed = tool->user_data;
    int x0 = (int)(ed->last_x + 0.5), y0 = (int)(ed->last_y + 0.5);
    int x1 = (int)(ev->x + 0.5), y1 = (int)(ev->y + 0.5);
    
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, cx = x0, cy = y0;
    
    while (1) {
        erase_draw(ed, doc, cx, cy);
        if (cx == x1 && cy == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; cx += sx; }
        if (e2 <= dx) { err += dx; cy += sy; }
    }
    ed->last_x = ev->x; ed->last_y = ev->y;
}

static void on_size_changed(GtkSpinButton *spin, gpointer ud) { ((SoftEraserData *)ud)->radius = gtk_spin_button_get_value_as_int(spin); }
static void on_hardness_changed(GtkRange *range, gpointer ud) { ((SoftEraserData *)ud)->hardness = gtk_range_get_value(range); }
static void on_opacity_changed(GtkRange *range, gpointer ud) { ((SoftEraserData *)ud)->opacity = gtk_range_get_value(range); }

static GtkWidget *soft_eraser_build_options(Tool *tool) {
    SoftEraserData *ed = tool->user_data;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget *r1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(r1), gtk_label_new("Size:"));
    GtkWidget *spin = gtk_spin_button_new_with_range(1, 200, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), ed->radius);
    g_signal_connect(spin, "value-changed", G_CALLBACK(on_size_changed), ed);
    gtk_widget_set_hexpand(spin, TRUE);
    gtk_box_append(GTK_BOX(r1), spin);
    gtk_box_append(GTK_BOX(box), r1);

    GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(r2), gtk_label_new("Hardness:"));
    GtkWidget *hard = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.05);
    gtk_widget_set_hexpand(hard, TRUE);
    gtk_range_set_value(GTK_RANGE(hard), ed->hardness);
    g_signal_connect(hard, "value-changed", G_CALLBACK(on_hardness_changed), ed);
    gtk_box_append(GTK_BOX(r2), hard);
    gtk_box_append(GTK_BOX(box), r2);

    GtkWidget *r3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(r3), gtk_label_new("Opacity:"));
    GtkWidget *op = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 1.0, 0.05);
    gtk_widget_set_hexpand(op, TRUE);
    gtk_range_set_value(GTK_RANGE(op), ed->opacity);
    g_signal_connect(op, "value-changed", G_CALLBACK(on_opacity_changed), ed);
    gtk_box_append(GTK_BOX(r3), op);
    gtk_box_append(GTK_BOX(box), r3);

    return box;
}

static const ToolVTable soft_eraser_vtable = {
    .id = "soft_eraser",
    .group_id = "eraser", .name = "Soft Eraser", .icon_name = "edit-clear-symbolic",
     
    .description = "Soft eraser with falloff.",
    .activate = NULL, .deactivate = NULL,
    .stroke_begin = soft_eraser_begin, .stroke_move = soft_eraser_move, .stroke_end = NULL,
    .build_options_ui = soft_eraser_build_options,
};

Tool *soft_eraser_tool_new(void) {
    Tool *t = g_new0(Tool, 1);
    t->vtable = &soft_eraser_vtable;
    SoftEraserData *ed = g_new0(SoftEraserData, 1);
    ed->radius = 15; ed->hardness = 0.5f; ed->opacity = 1.0f;
    t->user_data = ed;
    return t;
}
