#include "rotate.h"
#include "../input_event.h"
#include "../transform.h"
#include "../tool_engine.h"
#include <math.h>

typedef struct {
    double start_angle;
    double orig_angle;
    gboolean is_moving;
    int drag_start_x, drag_start_y;
    double orig_center_x, orig_center_y;
    GtkWidget *scale;
} RotateData;

static void on_angle_changed(GtkRange *range, gpointer ud) {
    (void)ud;
    TransformContext *ctx = transform_get();
    if (!ctx->active) return;
    ctx->angle = gtk_range_get_value(range);
    Document *doc = tool_engine_get_document();
    if (doc) transform_update(doc);
}

static void update_angle_ui(RotateData *rd, TransformContext *ctx) {
    if (rd->scale) {
        g_signal_handlers_block_by_func(rd->scale, G_CALLBACK(on_angle_changed), rd);
        gtk_range_set_value(GTK_RANGE(rd->scale), ctx->angle);
        g_signal_handlers_unblock_by_func(rd->scale, G_CALLBACK(on_angle_changed), rd);
    }
}

static void on_btn_angle(GtkButton *b, gpointer ud) {
    (void)b;
    double angle = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "angle"));
    TransformContext *ctx = transform_get();
    if (!ctx->active) return;
    ctx->angle = angle;
    update_angle_ui(ud, ctx);
    Document *doc = tool_engine_get_document();
    if (doc) transform_update(doc);
}

static void on_btn_flip_h(GtkButton *b, gpointer ud) {
    (void)b;
    TransformContext *ctx = transform_get();
    if (!ctx->active) return;
    ctx->flip_h = !ctx->flip_h;
    Document *doc = tool_engine_get_document();
    if (doc) transform_update(doc);
}

static void on_btn_flip_v(GtkButton *b, gpointer ud) {
    (void)b;
    TransformContext *ctx = transform_get();
    if (!ctx->active) return;
    ctx->flip_v = !ctx->flip_v;
    Document *doc = tool_engine_get_document();
    if (doc) transform_update(doc);
}

static void rotate_begin(Tool *tool, Document *doc, InputEvent *ev) {
    RotateData *rd = tool->user_data;
    TransformContext *ctx = transform_get();
    
    if (!ctx->active) {
        if (document_has_selection(doc)) transform_begin(doc);
        if (!ctx->active) return;
    }

    // Commit if clicked far outside the current selection
    SelectionRect r = document_selection(doc);
    if (ev->x < r.x - 20 || ev->x > r.x + r.w + 20 || ev->y < r.y - 20 || ev->y > r.y + r.h + 20) {
        transform_commit(doc);
        document_clear_selection(doc);
        return;
    }

    double cx = ctx->cur_x + ctx->cur_w / 2.0;
    double cy = ctx->cur_y + ctx->cur_h / 2.0;
    rd->start_angle = atan2(ev->y - cy, ev->x - cx);
    rd->orig_angle = ctx->angle;
    
    rd->drag_start_x = (int)(ev->x + 0.5);
    rd->drag_start_y = (int)(ev->y + 0.5);
    rd->orig_center_x = ctx->ref_center_x;
    rd->orig_center_y = ctx->ref_center_y;

    double dist = hypot(ev->x - cx, ev->y - cy);
    rd->is_moving = (dist < (ctx->cur_w / 4.0));
}

static void rotate_move(Tool *tool, Document *doc, InputEvent *ev) {
    RotateData *rd = tool->user_data;
    TransformContext *ctx = transform_get();
    if (!ctx->active) return;

    if (rd->is_moving) {
        int dx = (int)(ev->x + 0.5) - rd->drag_start_x;
        int dy = (int)(ev->y + 0.5) - rd->drag_start_y;
        ctx->ref_center_x = rd->orig_center_x + dx;
        ctx->ref_center_y = rd->orig_center_y + dy;
    } else {
        double cx = ctx->cur_x + ctx->cur_w / 2.0;
        double cy = ctx->cur_y + ctx->cur_h / 2.0;
        double cur_angle = atan2(ev->y - cy, ev->x - cx);
        double delta = (cur_angle - rd->start_angle) * 180.0 / G_PI;
        
        ctx->angle = rd->orig_angle + delta;
        while (ctx->angle > 180) ctx->angle -= 360;
        while (ctx->angle < -180) ctx->angle += 360;

        update_angle_ui(rd, ctx);
    }
    transform_update(doc);
}

static void rotate_end(Tool *tool, Document *doc, InputEvent *ev) {
    (void)tool; (void)ev;
    TransformContext *ctx = transform_get();
    if (ctx->active) {
        int snap = (int)(round(ctx->angle / 90.0) * 90);
        if (fabs(ctx->angle - snap) < 5.0) ctx->angle = snap;
        transform_update(doc);
    }
}

static GtkWidget *rotate_build_options(Tool *tool) {
    RotateData *rd = tool->user_data;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    
    // Reset slider if not active
    TransformContext *ctx = transform_get();
    double initial_angle = ctx->active ? ctx->angle : 0;

    GtkWidget *lbl = gtk_label_new("Angle:");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -180.0, 180.0, 1.0);
    gtk_range_set_value(GTK_RANGE(scale), initial_angle);
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_angle_changed), rd);
    rd->scale = scale;

    GtkWidget *row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(row1), TRUE);
    GtkWidget *b90 = gtk_button_new_with_label("90°");
    GtkWidget *b180 = gtk_button_new_with_label("180°");
    GtkWidget *b270 = gtk_button_new_with_label("270°");
    g_object_set_data(G_OBJECT(b90), "angle", GINT_TO_POINTER(90));
    g_object_set_data(G_OBJECT(b180), "angle", GINT_TO_POINTER(180));
    g_object_set_data(G_OBJECT(b270), "angle", GINT_TO_POINTER(270));
    g_signal_connect(b90, "clicked", G_CALLBACK(on_btn_angle), rd);
    g_signal_connect(b180, "clicked", G_CALLBACK(on_btn_angle), rd);
    g_signal_connect(b270, "clicked", G_CALLBACK(on_btn_angle), rd);
    gtk_box_append(GTK_BOX(row1), b90);
    gtk_box_append(GTK_BOX(row1), b180);
    gtk_box_append(GTK_BOX(row1), b270);

    GtkWidget *row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(row2), TRUE);
    GtkWidget *bflh = gtk_button_new_with_label("Mirror H");
    GtkWidget *bflv = gtk_button_new_with_label("Mirror V");
    g_signal_connect(bflh, "clicked", G_CALLBACK(on_btn_flip_h), rd);
    g_signal_connect(bflv, "clicked", G_CALLBACK(on_btn_flip_v), rd);
    gtk_box_append(GTK_BOX(row2), bflh);
    gtk_box_append(GTK_BOX(row2), bflv);

    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), scale);
    gtk_box_append(GTK_BOX(box), row1);
    gtk_box_append(GTK_BOX(box), row2);
    return box;
}

static const ToolVTable rotate_vtable = {
    .id = "rotate", .name = "Rotate", .icon_name = "object-rotate-right-symbolic",
    .group_id = "transform",
    .description = "Rotate and flip selected pixels.",
    .stroke_begin = rotate_begin, .stroke_move = rotate_move, .stroke_end = rotate_end,
    .build_options_ui = rotate_build_options,
};

Tool *rotate_tool_new(void) {
    Tool *t = g_new0(Tool, 1);
    t->vtable = &rotate_vtable;
    t->user_data = g_new0(RotateData, 1);
    return t;
}
