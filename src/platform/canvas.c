#include "canvas.h"
#include "checkerboard.h"
#include "../interaction/tool_engine.h"
#include "../interaction/input_event.h"
#include "../util/log.h"
#include <graphene.h>
#include <string.h>

enum { SIGNAL_CURSOR_MOVED, SIGNAL_ZOOM_CHANGED, SIGNAL_PICK_COLOR, N_SIGNALS };
static guint canvas_signals[N_SIGNALS];

struct _PaintCanvas {
    GtkWidget  parent_instance;
    Document  *document;
    GdkTexture *texture;

    GtkGesture         *stylus;
    GtkGesture         *click_lmb, *click_mmb, *click_rmb;
    GtkEventController *motion;
    GtkEventController *scroll;

    gboolean drawing;
    gboolean panning;
    double   pan_start_wx, pan_start_wy;
    double   pan_start_h,  pan_start_v;

    double   zoom;
    GtkAdjustment *hadj, *vadj;

    double   cursor_ix, cursor_iy;
    gboolean cursor_visible;
    double   cursor_radius;

    DocumentObserverHandle doc_obs;
};

G_DEFINE_FINAL_TYPE (PaintCanvas, paint_canvas, GTK_TYPE_WIDGET)

static GdkTexture *
build_texture_from_doc (Document *doc)
{
    int w = document_width (doc);
    int h = document_height (doc);
    const Pixel *src = (const Pixel *)document_composite (doc);
    if (!src || w <= 0 || h <= 0) return NULL;
    gsize n = (gsize)w * h;
    Pixel *buf = g_new (Pixel, n);
    for (gsize i = 0; i < n; i++) {
        Pixel p = src[i];
        if (p.a == 255) buf[i] = p;
        else if (p.a == 0) { buf[i].r = buf[i].g = buf[i].b = buf[i].a = 0; }
        else {
            buf[i].r = (uint8_t)((p.r * p.a + 127) / 255);
            buf[i].g = (uint8_t)((p.g * p.a + 127) / 255);
            buf[i].b = (uint8_t)((p.b * p.a + 127) / 255);
            buf[i].a = p.a;
        }
    }
    GBytes *bytes = g_bytes_new_take (buf, n * sizeof (Pixel));
    GdkTexture *tex = gdk_memory_texture_new (w, h,
        GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, w * 4);
    g_bytes_unref (bytes);
    return tex;
}

static void
paint_canvas_rebuild_texture (PaintCanvas *self)
{
    if (!self->document) return;
    GdkTexture *tex = build_texture_from_doc (self->document);
    if (!tex) return;
    g_clear_object (&self->texture);
    self->texture = tex;
}

static void
canvas_on_composite_changed (void *ud)
{
    PaintCanvas *self = PAINT_CANVAS (ud);
    paint_canvas_rebuild_texture (self);
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

static void
canvas_on_selection_changed (void *ud)
{
    gtk_widget_queue_draw (GTK_WIDGET (PAINT_CANVAS (ud)));
}

static const DocumentObserver canvas_observer = {
    .on_composite_changed    = canvas_on_composite_changed,
    .on_layer_list_changed   = NULL,
    .on_active_layer_changed = NULL,
    .on_selection_changed    = canvas_on_selection_changed,
};

static void
paint_canvas_snapshot (GtkWidget *widget, GtkSnapshot *snapshot)
{
    PaintCanvas *self = PAINT_CANVAS (widget);
    if (!self->document) return;

    double dw = document_width  (self->document) * self->zoom;
    double dh = document_height (self->document) * self->zoom;
    graphene_rect_t doc_rect = GRAPHENE_RECT_INIT (0, 0, dw, dh);

    gtk_snapshot_push_repeat (snapshot, &doc_rect, NULL);
    { graphene_rect_t tile = GRAPHENE_RECT_INIT (0, 0, 16, 16);
      gtk_snapshot_append_texture (snapshot, checkerboard_texture (), &tile); }
    gtk_snapshot_pop (snapshot);

    if (self->texture)
        gtk_snapshot_append_texture (snapshot, self->texture, &doc_rect);

    /* Selection outline — solid white + black inner for contrast */
    if (document_has_selection (self->document)) {
        SelectionRect r = document_selection (self->document);
        double x = r.x * self->zoom, y = r.y * self->zoom;
        double w = r.w * self->zoom, h = r.h * self->zoom;
        GdkRGBA white = { 1, 1, 1, 1 };
        GdkRGBA black = { 0, 0, 0, 1 };
        graphene_rect_t t;
        t = GRAPHENE_RECT_INIT (x, y, w, 1);           gtk_snapshot_append_color (snapshot, &white, &t);
        t = GRAPHENE_RECT_INIT (x, y + h - 1, w, 1);   gtk_snapshot_append_color (snapshot, &white, &t);
        t = GRAPHENE_RECT_INIT (x, y, 1, h);           gtk_snapshot_append_color (snapshot, &white, &t);
        t = GRAPHENE_RECT_INIT (x + w - 1, y, 1, h);   gtk_snapshot_append_color (snapshot, &white, &t);
        if (w > 2 && h > 2) {
            t = GRAPHENE_RECT_INIT (x + 1, y + 1, w - 2, 1);         gtk_snapshot_append_color (snapshot, &black, &t);
            t = GRAPHENE_RECT_INIT (x + 1, y + h - 2, w - 2, 1);     gtk_snapshot_append_color (snapshot, &black, &t);
            t = GRAPHENE_RECT_INIT (x + 1, y + 1, 1, h - 2);         gtk_snapshot_append_color (snapshot, &black, &t);
            t = GRAPHENE_RECT_INIT (x + w - 2, y + 1, 1, h - 2);     gtk_snapshot_append_color (snapshot, &black, &t);
        }
    }

    if (self->cursor_visible) {
        double cx = self->cursor_ix * self->zoom;
        double cy = self->cursor_iy * self->zoom;
        GdkRGBA white = { 1, 1, 1, 0.95 };
        GdkRGBA black = { 0, 0, 0, 0.95 };

        graphene_rect_t t;
        t = GRAPHENE_RECT_INIT (cx - 1, cy - 7, 2, 14);
        gtk_snapshot_append_color (snapshot, &white, &t);
        t = GRAPHENE_RECT_INIT (cx - 7, cy - 1, 14, 2);
        gtk_snapshot_append_color (snapshot, &white, &t);
        t = GRAPHENE_RECT_INIT (cx - 0.5, cy - 6, 1, 12);
        gtk_snapshot_append_color (snapshot, &black, &t);
        t = GRAPHENE_RECT_INIT (cx - 6, cy - 0.5, 12, 1);
        gtk_snapshot_append_color (snapshot, &black, &t);

        if (self->cursor_radius >= 1) {
            double r = self->cursor_radius * self->zoom;
            double x0 = cx - r, y0 = cy - r, x1 = cx + r, y1 = cy + r;
            t = GRAPHENE_RECT_INIT (x0, y0, x1 - x0, 1); gtk_snapshot_append_color (snapshot, &white, &t);
            t = GRAPHENE_RECT_INIT (x0, y1 - 1, x1 - x0, 1); gtk_snapshot_append_color (snapshot, &white, &t);
            t = GRAPHENE_RECT_INIT (x0, y0, 1, y1 - y0); gtk_snapshot_append_color (snapshot, &white, &t);
            t = GRAPHENE_RECT_INIT (x1 - 1, y0, 1, y1 - y0); gtk_snapshot_append_color (snapshot, &white, &t);
        }
    }
}

static void
paint_canvas_measure (GtkWidget *widget, GtkOrientation orientation,
                      int for_size, int *minimum, int *natural,
                      int *min_baseline, int *nat_baseline)
{
    (void)for_size;
    PaintCanvas *self = PAINT_CANVAS (widget);
    int size = 100;
    if (self->document) {
        double v = (orientation == GTK_ORIENTATION_HORIZONTAL)
                 ? document_width  (self->document) * self->zoom
                 : document_height (self->document) * self->zoom;
        size = (int)(v + 0.5);
    }
    *minimum = *natural = size;
    if (min_baseline) *min_baseline = -1;
    if (nat_baseline) *nat_baseline = -1;
}

/* ── Helper: emit cursor + return image coords ──────────────────────── */
static void
update_cursor (PaintCanvas *self, double wx, double wy, double *ix, double *iy)
{
    *ix = wx / self->zoom;
    *iy = wy / self->zoom;
    self->cursor_ix = *ix;
    self->cursor_iy = *iy;
    self->cursor_visible = TRUE;
    g_signal_emit (self, canvas_signals[SIGNAL_CURSOR_MOVED], 0, *ix, *iy);
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

/* ── Motion: widget coords directly from GTK ────────────────────────── */
static void
on_motion (GtkEventControllerMotion *ctrl, double x, double y, gpointer ud)
{
    (void)ctrl;
    PaintCanvas *self = PAINT_CANVAS (ud);
    double ix, iy;
    update_cursor (self, x, y, &ix, &iy);

    if (self->panning && self->hadj && self->vadj) {
        double dx = x - self->pan_start_wx;
        double dy = y - self->pan_start_wy;
        gtk_adjustment_set_value (self->hadj, self->pan_start_h - dx);
        gtk_adjustment_set_value (self->vadj, self->pan_start_v - dy);
    } else if (self->drawing && self->document) {
        InputEvent ev = { .x = ix, .y = iy, .pressure = 1.0 };
        tool_engine_dispatch_move (self->document, &ev);
    }
}

/* ── LMB: drawing ───────────────────────────────────────────────────── */
static void
on_lmb_pressed (GtkGestureClick *g, int npress, double x, double y, gpointer ud)
{
    (void)g; (void)npress;
    PaintCanvas *self = PAINT_CANVAS (ud);
    if (!self->document) return;
    double ix, iy;
    update_cursor (self, x, y, &ix, &iy);
    self->drawing = TRUE;
    InputEvent ev = { .x = ix, .y = iy, .pressure = 1.0 };
    tool_engine_dispatch_begin (self->document, &ev);
}

static void
on_lmb_released (GtkGestureClick *g, int npress, double x, double y, gpointer ud)
{
    (void)g; (void)npress;
    PaintCanvas *self = PAINT_CANVAS (ud);
    if (!self->drawing) return;
    self->drawing = FALSE;
    if (!self->document) return;
    double ix = x / self->zoom;
    double iy = y / self->zoom;
    InputEvent ev = { .x = ix, .y = iy, .pressure = 1.0 };
    tool_engine_dispatch_end (self->document, &ev);
}

/* ── MMB: pan ───────────────────────────────────────────────────────── */
static void
on_mmb_pressed (GtkGestureClick *g, int npress, double x, double y, gpointer ud)
{
    (void)g; (void)npress;
    PaintCanvas *self = PAINT_CANVAS (ud);
    if (!self->hadj || !self->vadj) return;
    self->panning = TRUE;
    self->pan_start_wx = x;
    self->pan_start_wy = y;
    self->pan_start_h = gtk_adjustment_get_value (self->hadj);
    self->pan_start_v = gtk_adjustment_get_value (self->vadj);
}
static void
on_mmb_released (GtkGestureClick *g, int npress, double x, double y, gpointer ud)
{
    (void)g; (void)npress; (void)x; (void)y;
    PAINT_CANVAS (ud)->panning = FALSE;
}

/* ── RMB: pick colour (this is the Ctrl+RMB shortcut) ──────────────── */
static void
on_rmb_pressed (GtkGestureClick *g, int npress, double x, double y, gpointer ud)
{
    (void)g; (void)npress;
    PaintCanvas *self = PAINT_CANVAS (ud);
    double ix = x / self->zoom;
    double iy = y / self->zoom;
    g_signal_emit (self, canvas_signals[SIGNAL_PICK_COLOR], 0, ix, iy);
}

/* ── Stylus: widget coords already provided by gesture ─────────────── */
static void
fill_stylus (GtkGestureStylus *g, PaintCanvas *self, double x, double y, InputEvent *ev)
{
    ev->x = x / self->zoom;
    ev->y = y / self->zoom;
    ev->pressure = 1.0;
    ev->tilt_x = ev->tilt_y = ev->rotation = ev->distance = 0.0;
    ev->modifiers = 0;
    (void)gtk_gesture_stylus_get_axis (g, GDK_AXIS_PRESSURE, &ev->pressure);
    (void)gtk_gesture_stylus_get_axis (g, GDK_AXIS_XTILT,   &ev->tilt_x);
    (void)gtk_gesture_stylus_get_axis (g, GDK_AXIS_YTILT,   &ev->tilt_y);
}

static void
on_stylus_down (GtkGestureStylus *g, double x, double y, gpointer ud)
{
    PaintCanvas *self = PAINT_CANVAS (ud);
    if (!self->document) return;
    InputEvent ev; fill_stylus (g, self, x, y, &ev);
    double ix, iy;
    update_cursor (self, x, y, &ix, &iy);
    self->drawing = TRUE;
    tool_engine_dispatch_begin (self->document, &ev);
}

static void
on_stylus_motion (GtkGestureStylus *g, double x, double y, gpointer ud)
{
    PaintCanvas *self = PAINT_CANVAS (ud);
    InputEvent ev; fill_stylus (g, self, x, y, &ev);
    double ix, iy;
    update_cursor (self, x, y, &ix, &iy);
    if (!self->drawing || !self->document) return;
    tool_engine_dispatch_move (self->document, &ev);
}

static void
on_stylus_up (GtkGestureStylus *g, double x, double y, gpointer ud)
{
    PaintCanvas *self = PAINT_CANVAS (ud);
    if (!self->drawing || !self->document) return;
    InputEvent ev; fill_stylus (g, self, x, y, &ev);
    self->drawing = FALSE;
    tool_engine_dispatch_end (self->document, &ev);
}

/* ── Ctrl+wheel zoom ────────────────────────────────────────────────── */
static gboolean
on_scroll (GtkEventControllerScroll *ctrl, double dx, double dy, gpointer ud)
{
    (void)dx;
    PaintCanvas *self = PAINT_CANVAS (ud);
    GdkModifierType mods = gtk_event_controller_get_current_event_state (
        GTK_EVENT_CONTROLLER (ctrl));
    if (!(mods & GDK_CONTROL_MASK))
        return GDK_EVENT_PROPAGATE;
    if (dy < 0) paint_canvas_zoom_in  (self);
    else        paint_canvas_zoom_out (self);
    return GDK_EVENT_STOP;
}

/* ── Zoom API ───────────────────────────────────────────────────────── */
static const double ZOOM_STEPS[] = {
    0.1, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0
};
static const int N_ZOOM = sizeof (ZOOM_STEPS) / sizeof (ZOOM_STEPS[0]);

double paint_canvas_get_zoom (PaintCanvas *self) { return self ? self->zoom : 1.0; }

void
paint_canvas_set_zoom (PaintCanvas *self, double zoom)
{
    if (!self) return;
    if (zoom < 0.1)  zoom = 0.1;
    if (zoom > 16.0) zoom = 16.0;
    if (zoom == self->zoom) return;
    self->zoom = zoom;
    gtk_widget_queue_resize (GTK_WIDGET (self));
    gtk_widget_queue_draw   (GTK_WIDGET (self));
    LOG_DEBUG ("zoom changed to %.2f", zoom);
    g_signal_emit (self, canvas_signals[SIGNAL_ZOOM_CHANGED], 0, zoom);
}

void
paint_canvas_zoom_in (PaintCanvas *self)
{
    if (!self) return;
    LOG_DEBUG ("zoom_in called (current %.2f)", self->zoom);
    for (int i = 0; i < N_ZOOM; i++)
        if (ZOOM_STEPS[i] > self->zoom + 0.001) {
            paint_canvas_set_zoom (self, ZOOM_STEPS[i]); return;
        }
}

void
paint_canvas_zoom_out (PaintCanvas *self)
{
    if (!self) return;
    LOG_DEBUG ("zoom_out called (current %.2f)", self->zoom);
    for (int i = N_ZOOM - 1; i >= 0; i--)
        if (ZOOM_STEPS[i] < self->zoom - 0.001) {
            paint_canvas_set_zoom (self, ZOOM_STEPS[i]); return;
        }
}

void
paint_canvas_zoom_fit (PaintCanvas *self, int vw, int vh)
{
    if (!self || !self->document || vw <= 0 || vh <= 0) return;
    double sx = (double)vw / document_width  (self->document);
    double sy = (double)vh / document_height (self->document);
    paint_canvas_set_zoom (self, sx < sy ? sx : sy);
}

void
paint_canvas_set_scroll_adjustments (PaintCanvas *self,
                                      GtkAdjustment *hadj, GtkAdjustment *vadj)
{
    if (!self) return;
    self->hadj = hadj;
    self->vadj = vadj;
}

void
paint_canvas_set_cursor_radius (PaintCanvas *self, double r)
{
    if (!self) return;
    self->cursor_radius = r;
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

/* ── Lifecycle ──────────────────────────────────────────────────────── */
static void
paint_canvas_dispose (GObject *obj)
{
    PaintCanvas *self = PAINT_CANVAS (obj);
    if (self->document && self->doc_obs) {
        document_remove_observer (self->document, self->doc_obs);
        self->doc_obs = NULL;
    }
    self->document = NULL;
    g_clear_object (&self->texture);
    G_OBJECT_CLASS (paint_canvas_parent_class)->dispose (obj);
}

static void
paint_canvas_init (PaintCanvas *self)
{
    self->zoom = 1.0;
    gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
    gtk_widget_set_hexpand (GTK_WIDGET (self), FALSE);
    gtk_widget_set_vexpand (GTK_WIDGET (self), FALSE);
    gtk_widget_set_halign  (GTK_WIDGET (self), GTK_ALIGN_START);
    gtk_widget_set_valign  (GTK_WIDGET (self), GTK_ALIGN_START);

    self->stylus = gtk_gesture_stylus_new ();
    g_signal_connect (self->stylus, "down",   G_CALLBACK (on_stylus_down),   self);
    g_signal_connect (self->stylus, "motion", G_CALLBACK (on_stylus_motion), self);
    g_signal_connect (self->stylus, "up",     G_CALLBACK (on_stylus_up),     self);
    gtk_widget_add_controller (GTK_WIDGET (self),
                               GTK_EVENT_CONTROLLER (self->stylus));

    self->click_lmb = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (self->click_lmb), 1);
    g_signal_connect (self->click_lmb, "pressed",  G_CALLBACK (on_lmb_pressed),  self);
    g_signal_connect (self->click_lmb, "released", G_CALLBACK (on_lmb_released), self);
    gtk_widget_add_controller (GTK_WIDGET (self),
                               GTK_EVENT_CONTROLLER (self->click_lmb));

    self->click_mmb = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (self->click_mmb), 2);
    g_signal_connect (self->click_mmb, "pressed",  G_CALLBACK (on_mmb_pressed),  self);
    g_signal_connect (self->click_mmb, "released", G_CALLBACK (on_mmb_released), self);
    gtk_widget_add_controller (GTK_WIDGET (self),
                               GTK_EVENT_CONTROLLER (self->click_mmb));

    self->click_rmb = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (self->click_rmb), 3);
    g_signal_connect (self->click_rmb, "pressed", G_CALLBACK (on_rmb_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self),
                               GTK_EVENT_CONTROLLER (self->click_rmb));

    self->motion = gtk_event_controller_motion_new ();
    g_signal_connect (self->motion, "motion", G_CALLBACK (on_motion), self);
    gtk_widget_add_controller (GTK_WIDGET (self), self->motion);

    self->scroll = gtk_event_controller_scroll_new (
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect (self->scroll, "scroll", G_CALLBACK (on_scroll), self);
    gtk_widget_add_controller (GTK_WIDGET (self), self->scroll);
}

static void
paint_canvas_class_init (PaintCanvasClass *klass)
{
    GTK_WIDGET_CLASS (klass)->snapshot = paint_canvas_snapshot;
    GTK_WIDGET_CLASS (klass)->measure  = paint_canvas_measure;
    G_OBJECT_CLASS   (klass)->dispose  = paint_canvas_dispose;

    canvas_signals[SIGNAL_CURSOR_MOVED] = g_signal_new (
        "cursor-moved", PAINT_TYPE_CANVAS, G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
    canvas_signals[SIGNAL_ZOOM_CHANGED] = g_signal_new (
        "zoom-changed", PAINT_TYPE_CANVAS, G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_DOUBLE);
    canvas_signals[SIGNAL_PICK_COLOR] = g_signal_new (
        "pick-color", PAINT_TYPE_CANVAS, G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
}

GtkWidget *paint_canvas_new (void) { return g_object_new (PAINT_TYPE_CANVAS, NULL); }

void
paint_canvas_set_document (PaintCanvas *self, Document *doc)
{
    if (!self) return;
    if (self->document == doc) return;
    if (self->document && self->doc_obs) {
        document_remove_observer (self->document, self->doc_obs);
        self->doc_obs = NULL;
    }
    self->document = doc;
    if (doc) {
        self->doc_obs = document_add_observer (doc, &canvas_observer, self);
        gtk_widget_queue_resize (GTK_WIDGET (self));
    } else {
        g_clear_object (&self->texture);
        gtk_widget_queue_draw (GTK_WIDGET (self));
    }
}

Document *paint_canvas_get_document (PaintCanvas *self)
{
    return self ? self->document : NULL;
}
