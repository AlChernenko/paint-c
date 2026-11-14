#include "swatch.h"

struct _PaintSwatch {
    GtkWidget parent_instance;
    GdkRGBA   color;
    gboolean  hover;
};

G_DEFINE_FINAL_TYPE (PaintSwatch, paint_swatch, GTK_TYPE_WIDGET)

static void
paint_swatch_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
    PaintSwatch *self = PAINT_SWATCH (widget);
    int w = gtk_widget_get_width (widget);
    int h = gtk_widget_get_height (widget);
    if (w <= 0 || h <= 0) return;

    graphene_rect_t r = GRAPHENE_RECT_INIT (0, 0, w, h);
    gtk_snapshot_append_color (snapshot, &self->color, &r);

    GdkRGBA border = { 0, 0, 0, 0.35 };
    graphene_rect_t t;
    t = GRAPHENE_RECT_INIT (0, 0, w, 1);         gtk_snapshot_append_color (snapshot, &border, &t);
    t = GRAPHENE_RECT_INIT (0, h - 1, w, 1);     gtk_snapshot_append_color (snapshot, &border, &t);
    t = GRAPHENE_RECT_INIT (0, 0, 1, h);         gtk_snapshot_append_color (snapshot, &border, &t);
    t = GRAPHENE_RECT_INIT (w - 1, 0, 1, h);     gtk_snapshot_append_color (snapshot, &border, &t);

    if (self->hover) {
        GdkRGBA hl = { 1, 1, 1, 0.55 };
        t = GRAPHENE_RECT_INIT (1, 1, w - 2, 1);         gtk_snapshot_append_color (snapshot, &hl, &t);
        t = GRAPHENE_RECT_INIT (1, h - 2, w - 2, 1);     gtk_snapshot_append_color (snapshot, &hl, &t);
        t = GRAPHENE_RECT_INIT (1, 1, 1, h - 2);         gtk_snapshot_append_color (snapshot, &hl, &t);
        t = GRAPHENE_RECT_INIT (w - 2, 1, 1, h - 2);     gtk_snapshot_append_color (snapshot, &hl, &t);
    }
}

static void
paint_swatch_measure (GtkWidget *widget, GtkOrientation orientation,
                      int for_size, int *minimum, int *natural,
                      int *min_baseline, int *nat_baseline)
{
    (void)widget; (void)for_size;
    *minimum = *natural = 22;
    if (min_baseline) *min_baseline = -1;
    if (nat_baseline) *nat_baseline = -1;
}

static void
on_hover_enter (GtkEventControllerMotion *c, double x, double y, gpointer ud)
{
    (void)c; (void)x; (void)y;
    PaintSwatch *self = PAINT_SWATCH (ud);
    self->hover = TRUE;
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
on_hover_leave (GtkEventControllerMotion *c, gpointer ud)
{
    (void)c;
    PaintSwatch *self = PAINT_SWATCH (ud);
    self->hover = FALSE;
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
paint_swatch_init (PaintSwatch *self)
{
    self->color = (GdkRGBA){ 0, 0, 0, 1 };
    gtk_widget_set_cursor_from_name (GTK_WIDGET (self), "pointer");
    gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);

    GtkEventController *motion = gtk_event_controller_motion_new ();
    g_signal_connect (motion, "enter", G_CALLBACK (on_hover_enter), self);
    g_signal_connect (motion, "leave", G_CALLBACK (on_hover_leave), self);
    gtk_widget_add_controller (GTK_WIDGET (self), motion);
}

static void
paint_swatch_class_init (PaintSwatchClass *klass)
{
    GTK_WIDGET_CLASS (klass)->snapshot = paint_swatch_snapshot;
    GTK_WIDGET_CLASS (klass)->measure  = paint_swatch_measure;
}

GtkWidget *
paint_swatch_new (const GdkRGBA *color)
{
    PaintSwatch *s = g_object_new (PAINT_TYPE_SWATCH, NULL);
    if (color) s->color = *color;
    return GTK_WIDGET (s);
}

void
paint_swatch_set_color (PaintSwatch *self, const GdkRGBA *color)
{
    if (!color) return;
    self->color = *color;
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
paint_swatch_get_color (PaintSwatch *self, GdkRGBA *out)
{
    *out = self->color;
}
