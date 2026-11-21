#include "navigator.h"
#include "../util/log.h"

struct _PaintNavigator {
    GtkWidget  parent_instance;
    Document  *document;
    GtkAdjustment *hadj, *vadj;
    double      zoom;
    GdkTexture *thumb;
    DocumentObserverHandle doc_obs;
};

G_DEFINE_FINAL_TYPE (PaintNavigator, paint_navigator, GTK_TYPE_WIDGET)

static void
rebuild_thumb (PaintNavigator *self)
{
    if (!self->document) return;
    int w = document_width (self->document);
    int h = document_height (self->document);
    const uint8_t *src = document_composite (self->document);
    if (!src || w <= 0 || h <= 0) return;

    /* The document composite is straight (unpremultiplied) BGRA.  GDK only
     * offers premultiplied BGRA textures, so convert first — otherwise
     * partially-transparent pixels (common with multiple layers) render
     * with dark fringes / wrong colors in the thumbnail. */
    gsize n = (gsize)w * h;
    uint8_t *buf = g_new (uint8_t, n * 4);
    for (gsize i = 0; i < n; i++) {
        const uint8_t *p = src + i * 4;
        uint8_t a = p[3];
        if (a == 255)      { buf[i*4+0] = p[0]; buf[i*4+1] = p[1]; buf[i*4+2] = p[2]; }
        else if (a == 0)   { buf[i*4+0] = buf[i*4+1] = buf[i*4+2] = 0; }
        else {
            buf[i*4+0] = (uint8_t)((p[0] * a + 127) / 255);
            buf[i*4+1] = (uint8_t)((p[1] * a + 127) / 255);
            buf[i*4+2] = (uint8_t)((p[2] * a + 127) / 255);
        }
        buf[i*4+3] = a;
    }
    GBytes *bytes = g_bytes_new_take (buf, n * 4);
    GdkTexture *tex = gdk_memory_texture_new (w, h,
        GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, w * 4);
    g_bytes_unref (bytes);
    g_clear_object (&self->thumb);
    self->thumb = tex;
}

static void
nav_on_doc_changed (void *ud)
{
    PaintNavigator *self = PAINT_NAVIGATOR (ud);
    rebuild_thumb (self);
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

static const DocumentObserver nav_observer = {
    .on_composite_changed    = nav_on_doc_changed,
    .on_layer_list_changed   = NULL,
    .on_active_layer_changed = NULL,
};

static void
nav_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
    PaintNavigator *self = PAINT_NAVIGATOR (widget);
    if (!self->document || !self->thumb) return;

    int ww = gtk_widget_get_width (widget);
    int wh = gtk_widget_get_height (widget);
    if (ww <= 0 || wh <= 0) return;

    int dw = document_width (self->document);
    int dh = document_height (self->document);
    if (dw <= 0 || dh <= 0) return;

    /* Fit doc into widget preserving aspect. */
    double sx = (double)ww / dw;
    double sy = (double)wh / dh;
    double s  = sx < sy ? sx : sy;
    double sw = dw * s, sh = dh * s;
    double ox = (ww - sw) / 2, oy = (wh - sh) / 2;

    graphene_rect_t doc_rect = GRAPHENE_RECT_INIT (ox, oy, sw, sh);
    gtk_snapshot_append_texture (snapshot, self->thumb, &doc_rect);

    /* Viewport rectangle */
    if (self->hadj && self->vadj && self->zoom > 0) {
        double vx0 = gtk_adjustment_get_value (self->hadj);
        double vy0 = gtk_adjustment_get_value (self->vadj);
        double vw  = gtk_adjustment_get_page_size (self->hadj);
        double vh  = gtk_adjustment_get_page_size (self->vadj);

        double dx0 = vx0 / self->zoom;
        double dy0 = vy0 / self->zoom;
        double dwd = vw  / self->zoom;
        double dhd = vh  / self->zoom;

        double rx = ox + (dx0 / dw) * sw;
        double ry = oy + (dy0 / dh) * sh;
        double rw = (dwd / dw) * sw;
        double rh = (dhd / dh) * sh;

        GdkRGBA col = { 1.0, 0.2, 0.2, 1.0 };
        float w = 2.0f;
        graphene_rect_t t;

        t = GRAPHENE_RECT_INIT (rx, ry, rw, w);
        gtk_snapshot_append_color (snapshot, &col, &t);
        t = GRAPHENE_RECT_INIT (rx, ry + rh - w, rw, w);
        gtk_snapshot_append_color (snapshot, &col, &t);
        t = GRAPHENE_RECT_INIT (rx, ry, w, rh);
        gtk_snapshot_append_color (snapshot, &col, &t);
        t = GRAPHENE_RECT_INIT (rx + rw - w, ry, w, rh);
        gtk_snapshot_append_color (snapshot, &col, &t);
    }
}

static void
nav_measure (GtkWidget *widget, GtkOrientation orientation,
             int for_size, int *minimum, int *natural,
             int *min_baseline, int *nat_baseline)
{
    (void)widget; (void)for_size;
    *minimum = *natural = 180;
    if (min_baseline) *min_baseline = -1;
    if (nat_baseline) *nat_baseline = -1;
}

static void
nav_click (GtkGestureClick *g, int npress, double x, double y, gpointer ud)
{
    (void)npress;
    PaintNavigator *self = PAINT_NAVIGATOR (ud);
    if (!self->document || !self->hadj || !self->vadj || self->zoom <= 0) return;

    int ww = gtk_widget_get_width (GTK_WIDGET (self));
    int wh = gtk_widget_get_height (GTK_WIDGET (self));
    int dw = document_width (self->document);
    int dh = document_height (self->document);
    if (ww <= 0 || wh <= 0 || dw <= 0 || dh <= 0) return;

    double s = ((double)ww / dw < (double)wh / dh) ? (double)ww / dw : (double)wh / dh;
    double sw = dw * s, sh = dh * s;
    double ox = (ww - sw) / 2, oy = (wh - sh) / 2;

    /* Click position -> doc coords */
    double dx = (x - ox) / s;
    double dy = (y - oy) / s;

    /* Center viewport at that point */
    double new_h = dx * self->zoom - gtk_adjustment_get_page_size (self->hadj) / 2;
    double new_v = dy * self->zoom - gtk_adjustment_get_page_size (self->vadj) / 2;

    gtk_adjustment_set_value (self->hadj, MAX (new_h, 0));
    gtk_adjustment_set_value (self->vadj, MAX (new_v, 0));
}

static void
nav_dispose (GObject *obj)
{
    PaintNavigator *self = PAINT_NAVIGATOR (obj);
    if (self->document && self->doc_obs) {
        document_remove_observer (self->document, self->doc_obs);
        self->doc_obs = NULL;
    }
    self->document = NULL;
    g_clear_object (&self->thumb);
    G_OBJECT_CLASS (paint_navigator_parent_class)->dispose (obj);
}

static void
paint_navigator_init (PaintNavigator *self)
{
    gtk_widget_set_size_request (GTK_WIDGET (self), 180, 140);
    gtk_widget_set_vexpand (GTK_WIDGET (self), FALSE);
    GtkGesture *click = gtk_gesture_click_new ();
    g_signal_connect (click, "pressed", G_CALLBACK (nav_click), self);
    gtk_widget_add_controller (GTK_WIDGET (self),
                               GTK_EVENT_CONTROLLER (click));
}

static void
paint_navigator_class_init (PaintNavigatorClass *klass)
{
    GTK_WIDGET_CLASS (klass)->snapshot = nav_snapshot;
    GTK_WIDGET_CLASS (klass)->measure  = nav_measure;
    G_OBJECT_CLASS   (klass)->dispose  = nav_dispose;
}

GtkWidget *paint_navigator_new (void) { return g_object_new (PAINT_TYPE_NAVIGATOR, NULL); }

void
paint_navigator_set_document (PaintNavigator *self, Document *doc)
{
    if (self->document && self->doc_obs) {
        document_remove_observer (self->document, self->doc_obs);
        self->doc_obs = NULL;
    }
    self->document = doc;
    if (doc) {
        self->doc_obs = document_add_observer (doc, &nav_observer, self);
    } else {
        g_clear_object (&self->thumb);
    }
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
paint_navigator_set_scroll (PaintNavigator *self,
                            GtkAdjustment *hadj, GtkAdjustment *vadj,
                            double zoom)
{
    self->hadj = hadj;
    self->vadj = vadj;
    self->zoom = zoom;
    gtk_widget_queue_draw (GTK_WIDGET (self));
}
