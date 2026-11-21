/* shape.c — parametric shape tool (rect, ellipse, triangle, star).
 *
 * LAYER: L3 (interaction). See shape.h.
 *
 * While dragging, the layer is restored from a snapshot and the shape is
 * re-rasterised as a live preview directly into the active layer buffer
 * (bypassing document_apply_* so history isn't spammed). The engine wraps
 * the whole gesture in begin/end_stroke, so one Ctrl+Z removes the shape.
 * Pressing Escape during the drag cancels: the snapshot is restored and no
 * pixels are committed.
 */

#include "shape.h"
#include "../input_event.h"
#include "../tool_engine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    ShapeKind kind;
    gboolean  filled;        /* TRUE = fill, FALSE = outline */
    int       stroke_width;
    Pixel     color;
    double    start_x, start_y;
    double    cur_x, cur_y;      /* last pointer position during the drag */
    gboolean  dragging;
    /* preview bookkeeping */
    int       snap_w, snap_h;
    PixelBuffer *snapshot;   /* layer contents before the gesture */
} ShapeData;

/* ── geometry helpers ─────────────────────────────────────────────── */

static gboolean
inside_shape (ShapeKind kind, double x, double y,
              double l, double t, double r, double b)
{
    double w = r - l, h = b - t;
    if (w <= 0 || h <= 0) return FALSE;
    switch (kind) {
    case SHAPE_RECT:
        return TRUE;                       /* rect region already clipped */
    case SHAPE_ELLIPSE: {
        double nx = (x - (l + w / 2)) / (w / 2);
        double ny = (y - (t + h / 2)) / (h / 2);
        return nx * nx + ny * ny <= 1.0;
    }
    case SHAPE_TRIANGLE: {
        /* apex top-centre, base along bottom */
        if (y >= b || y <= t) return FALSE;
        double f = (y - t) / h;             /* 0 at apex, 1 at base */
        double half = f * w / 2.0;
        double cx = l + w / 2.0;
        return x >= cx - half && x <= cx + half;
    }
    case SHAPE_STAR: {
        /* 5-point star, outer radius = half-size, inner = 0.4 */
        double cx = l + w / 2.0, cy = t + h / 2.0;
        double rx = w / 2.0, ry = h / 2.0;
        double ang = atan2 ((y - cy) / ry, (x - cx) / rx);
        if (ang < 0) ang += 2 * M_PI;
        ang -= -M_PI / 2.0;                 /* first point up */
        double seg = (2 * M_PI) / 10.0;
        double a = fmod (ang, seg);
        double frac = a / seg;
        double rr = 1.0 - 0.6 * (frac < 0.5 ? frac * 2 : (1 - frac) * 2);
        double d = hypot ((x - cx) / rx, (y - cy) / ry);
        return d <= rr;
    }
    default:
        return FALSE;
    }
}

static void
rasterise (ShapeData *sd, Layer *l, int w, int h,
           double x0, double y0, double x1, double y1)
{
    double left   = MIN (x0, x1), right  = MAX (x0, x1);
    double top    = MIN (y0, y1), bottom = MAX (y0, y1);
    /* expand by stroke width so outlines aren't clipped away */
    double pad = sd->filled ? 0 : sd->stroke_width;
    int ix0 = CLAMP ((int)floor (left - pad),      0, w - 1);
    int iy0 = CLAMP ((int)floor (top - pad),       0, h - 1);
    int ix1 = CLAMP ((int)ceil  (right + pad) + 1, 0, w);
    int iy1 = CLAMP ((int)ceil  (bottom + pad)+ 1, 0, h);

    for (int y = iy0; y < iy1; y++) {
        Pixel *row = &l->buffer->pixels[(gsize)y * w];
        for (int x = ix0; x < ix1; x++) {
            double px = x + 0.5, py = y + 0.5;
            gboolean in_out = inside_shape (sd->kind, px, py, left, top, right, bottom);
            gboolean paint  = FALSE;
            if (sd->filled) {
                paint = in_out;
            } else {
                double sw = MAX (1, sd->stroke_width);
                gboolean in_inner = inside_shape (sd->kind, px, py,
                                                  left + sw, top + sw,
                                                  right - sw, bottom - sw);
                paint = in_out && !in_inner;
            }
            if (paint) row[x] = sd->color;
        }
    }
}

static void
restore_snapshot (ShapeData *sd, Document *doc)
{
    Layer *l = document_layer_at (doc, document_active_layer (doc));
    if (!l || !sd->snapshot) return;
    int w = document_width (doc), h = document_height (doc);
    if (sd->snap_w != w || sd->snap_h != h) return;   /* doc resized mid-stroke */
    memcpy (l->buffer->pixels, sd->snapshot->pixels,
            (gsize)w * h * sizeof (Pixel));
    document_invalidate (doc);
}

/* ── stroke callbacks ─────────────────────────────────────────────── */

static void
shape_begin (Tool *tool, Document *doc, InputEvent *ev)
{
    ShapeData *sd = tool->user_data;
    Layer *l = document_layer_at (doc, document_active_layer (doc));
    if (!l) return;

    sd->start_x = ev->x; sd->start_y = ev->y;
    sd->cur_x = ev->x;   sd->cur_y = ev->y;
    sd->dragging = TRUE;

    int w = document_width (doc), h = document_height (doc);
    if (sd->snapshot) pixel_buffer_free (sd->snapshot);
    sd->snapshot = pixel_buffer_new (w, h);
    sd->snap_w = w; sd->snap_h = h;
    memcpy (sd->snapshot->pixels, l->buffer->pixels, (gsize)w * h * sizeof (Pixel));
}

static void
shape_move (Tool *tool, Document *doc, InputEvent *ev)
{
    ShapeData *sd = tool->user_data;
    if (!sd->dragging) return;
    sd->cur_x = ev->x; sd->cur_y = ev->y;
    restore_snapshot (sd, doc);
    Layer *l = document_layer_at (doc, document_active_layer (doc));
    if (!l) return;
    rasterise (sd, l, sd->snap_w, sd->snap_h,
               sd->start_x, sd->start_y, sd->cur_x, sd->cur_y);
}

static void
shape_end (Tool *tool, Document *doc, InputEvent *ev)
{
    ShapeData *sd = tool->user_data;
    if (!sd->dragging) return;
    sd->dragging = FALSE;
    shape_move (tool, doc, ev);          /* final commit of the preview */
    if (sd->snapshot) { pixel_buffer_free (sd->snapshot); sd->snapshot = NULL; }
}

static void
shape_deactivate (Tool *tool, Document *doc)
{
    ShapeData *sd = tool->user_data;
    if (sd->dragging) {                  /* Escape / tool switch → cancel */
        restore_snapshot (sd, doc);
        sd->dragging = FALSE;
    }
    if (sd->snapshot) { pixel_buffer_free (sd->snapshot); sd->snapshot = NULL; }
}

/* Re-run the preview with current parameters (used by the options UI while
 * dragging is still active). Safe to call when not dragging. */
static void
shape_refresh_preview (Tool *tool, Document *doc)
{
    ShapeData *sd = tool->user_data;
    if (!sd->dragging || !doc) return;
    restore_snapshot (sd, doc);
    Layer *l = document_layer_at (doc, document_active_layer (doc));
    if (!l) return;
    rasterise (sd, l, sd->snap_w, sd->snap_h,
               sd->start_x, sd->start_y, sd->cur_x, sd->cur_y);
}

/* ── options UI ───────────────────────────────────────────────────── */

static const char *shape_kind_names[] = { "Rectangle", "Ellipse", "Triangle", "Star" };

/* The tool keeps its own pointer so the options UI (which only receives the
 * ShapeData) can refresh the live preview after a parameter change. */
static Tool *g_shape_tool = NULL;

static void shape_refresh_preview (Tool *tool, Document *doc);

static void on_kind_changed (GtkDropDown *dd, GParamSpec *p, gpointer ud)
{
    (void)p;
    ((ShapeData *)ud)->kind = gtk_drop_down_get_selected (dd);
    if (g_shape_tool) shape_refresh_preview (g_shape_tool, tool_engine_get_document ());
}
static void on_fill_toggled (GtkCheckButton *cb, gpointer ud)
{
    ((ShapeData *)ud)->filled = gtk_check_button_get_active (cb);
    if (g_shape_tool) shape_refresh_preview (g_shape_tool, tool_engine_get_document ());
}
static void on_width_changed (GtkSpinButton *sp, gpointer ud)
{
    ((ShapeData *)ud)->stroke_width = gtk_spin_button_get_value_as_int (sp);
    if (g_shape_tool) shape_refresh_preview (g_shape_tool, tool_engine_get_document ());
}

static GtkWidget *
shape_build_options (Tool *tool)
{
    ShapeData *sd = tool->user_data;
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget *kdd = gtk_drop_down_new_from_strings (shape_kind_names);
    gtk_drop_down_set_selected (GTK_DROP_DOWN (kdd), sd->kind);
    g_signal_connect (kdd, "notify::selected", G_CALLBACK (on_kind_changed), sd);
    gtk_box_append (GTK_BOX (box), kdd);

    GtkWidget *fill = gtk_check_button_new_with_label ("Fill (off = outline)");
    gtk_check_button_set_active (GTK_CHECK_BUTTON (fill), sd->filled);
    g_signal_connect (fill, "toggled", G_CALLBACK (on_fill_toggled), sd);
    gtk_box_append (GTK_BOX (box), fill);

    GtkWidget *wrow = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append (GTK_BOX (wrow), gtk_label_new ("Stroke:"));
    GtkWidget *wspin = gtk_spin_button_new_with_range (1, 64, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (wspin), sd->stroke_width);
    g_signal_connect (wspin, "value-changed", G_CALLBACK (on_width_changed), sd);
    gtk_box_append (GTK_BOX (wrow), wspin);
    gtk_box_append (GTK_BOX (box), wrow);

    GtkWidget *hint = gtk_label_new ("Drag to draw · Esc cancels");
    gtk_widget_set_opacity (hint, 0.55);
    gtk_box_append (GTK_BOX (box), hint);
    return box;
}

static const ToolVTable shape_vtable = {
    .id = "shape",
    .group_id = "misc", .name = "Shape",
    .description = "Draw rectangles, ellipses, triangles and stars.",
    .icon_name = "star-new-symbolic",
    .activate = NULL, .deactivate = shape_deactivate,
    .stroke_begin = shape_begin, .stroke_move = shape_move, .stroke_end = shape_end,
    .build_options_ui = shape_build_options,
};

Tool *
shape_tool_new (void)
{
    Tool *t = g_new0 (Tool, 1);
    t->vtable = &shape_vtable;
    ShapeData *sd = g_new0 (ShapeData, 1);
    sd->kind = SHAPE_RECT;
    sd->filled = TRUE;
    sd->stroke_width = 3;
    sd->color = (Pixel){ 0, 0, 0, 255 };
    t->user_data = sd;
    g_shape_tool = t;                 /* single instance owns the preview UI */
    return t;
}

void
shape_tool_set_color (Tool *tool, Pixel color)
{
    if (tool && tool->user_data) ((ShapeData *)tool->user_data)->color = color;
}

void
shape_tool_commit_or_cancel (Tool *tool, gboolean commit, double to_x, double to_y)
{
    g_return_if_fail (tool && tool->user_data);
    ShapeData *sd = tool->user_data;
    Document *doc = tool_engine_get_document ();
    if (!sd->dragging) return;

    if (commit) {
        InputEvent ev = { .x = to_x, .y = to_y };
        sd->dragging = FALSE;
        shape_move (tool, doc, &ev);
        if (sd->snapshot) { pixel_buffer_free (sd->snapshot); sd->snapshot = NULL; }
    } else {
        restore_snapshot (sd, doc);
        sd->dragging = FALSE;
        if (sd->snapshot) { pixel_buffer_free (sd->snapshot); sd->snapshot = NULL; }
    }
    document_invalidate (doc);
}
