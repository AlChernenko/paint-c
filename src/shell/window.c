#include "window.h"
#include "swatch.h"
#include "../domain/document.h"
#include "../platform/canvas.h"
#include "../platform/file_io.h"
#include "../platform/clipboard.h"
#include "../platform/navigator.h"
#include "../interaction/tool_engine.h"
#include "../interaction/tools/pencil.h"
#include "../interaction/tools/eraser.h"
#include "../interaction/tools/eyedropper.h"
#include "../interaction/tools/blur.h"
#include "../interaction/tools/select_rect.h"
#include "../plugin/registry.h"
#include "../util/log.h"
#include "../util/config.h"
#include <string.h>

struct _PaintWindow {
    GtkApplicationWindow parent_instance;

    Document *document;
    char     *current_path;

    Tool *pencil, *eraser, *eyedropper, *blur, *select;
    GPtrArray *plugin_tools;             /* Tool* created from addons */

    GtkWidget *canvas;
    GtkWidget *layer_list;
    GtkWidget *color_button;
    GtkWidget *radius_spin;
    GtkWidget *opacity_scale;
    GtkWidget *navigator;
    GtkAdjustment *hadj, *vadj;

    GtkWidget *tb_pencil, *tb_eraser, *tb_eyedropper, *tb_blur, *tb_select;

    GSimpleAction *act_layer_delete, *act_layer_up, *act_layer_down, *act_layer_merge;
    GSimpleAction *act_cut, *act_copy, *act_paste, *act_paste_layer;
    GSimpleAction *act_select_all, *act_deselect, *act_delete_sel;

    DocumentObserverHandle doc_obs;
    gboolean updating_ui;

    GtkWidget *st_doc, *st_layer, *st_tool, *st_cursor, *st_zoom;
    GtkWidget *st_hint;
    GtkWidget *st_dim;
    GtkWidget *panel_tools;
    GtkWidget *panel_colors;
    GtkWidget *panel_history;
    GtkWidget *panel_layers;
    GtkWidget *tool_options_box;
    GtkWidget *overlay;
    bool panel_tools_visible;
    bool panel_colors_visible;
    bool panel_history_visible;
    bool panel_layers_visible;
};

G_DEFINE_FINAL_TYPE (PaintWindow, paint_window, GTK_TYPE_APPLICATION_WINDOW)

static void update_status_bar (PaintWindow *self);
static void on_btn_zoom_fit (GtkButton *b, gpointer ud);
static void on_btn_zoom_out (GtkButton *b, gpointer ud);
static void on_btn_zoom_in  (GtkButton *b, gpointer ud);
static void on_btn_about    (GtkButton *b, gpointer ud);
static void on_btn_prefs    (GtkButton *b, gpointer ud);
static void update_cursor_radius (PaintWindow *self);
static GtkWidget *build_tool_palette (PaintWindow *self);
static void on_pick_color (PaintCanvas *canvas, double ix, double iy, gpointer ud);
static GtkWidget *build_history_panel (PaintWindow *);
static GtkWidget *build_color_panel (PaintWindow *);
static GtkWidget *build_tool_options_bar (PaintWindow *);
static GtkWidget *build_action_toolbar (PaintWindow *);
static void tool_options_refresh (PaintWindow *);
static const char *tool_hint (const char *id);
static void action_toggle_panel (GSimpleAction *, GVariant *, gpointer);
static void action_preferences (GSimpleAction *, GVariant *, gpointer);

static void
on_doc_selection_changed (void *ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    /* Actions stay enabled; their handlers no-op with no selection. */
    update_status_bar (self);
}



static void
update_title (PaintWindow *self)
{
    if (!self->document) return;
    const char *raw  = self->current_path ? self->current_path : "Untitled";
    char *base = g_path_get_basename (raw);
    char *title = document_is_dirty (self->document)
        ? g_strdup_printf ("Paint C — *%s", base)
        : g_strdup_printf ("Paint C — %s", base);
    gtk_window_set_title (GTK_WINDOW (self), title);
    g_free (title); g_free (base);
}

static void
update_layer_actions (PaintWindow *self)
{
    if (!self->document) return;
    int n = document_layer_count (self->document);
    int a = document_active_layer (self->document);
    g_simple_action_set_enabled (self->act_layer_delete, n > 1);
    g_simple_action_set_enabled (self->act_layer_up,     a >= 0 && a < n - 1);
    g_simple_action_set_enabled (self->act_layer_down,   a > 0);
    g_simple_action_set_enabled (self->act_layer_merge,  a > 0);
}

static int
current_tool_radius (PaintWindow *self)
{
    Tool *t = tool_engine_get_active ();
    if (t == self->pencil)     return pencil_tool_get_radius (t);
    if (t == self->eraser)     return eraser_tool_get_radius (t);
    if (t == self->blur)       return blur_tool_get_radius (t);
    return 2;
}

static void
sync_radius_spin (PaintWindow *self)
{
    if (!self->radius_spin) return;
    if (!GTK_IS_SPIN_BUTTON (self->radius_spin)) { self->radius_spin = NULL; return; }
    self->updating_ui = TRUE;
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->radius_spin),
                               current_tool_radius (self));
    self->updating_ui = FALSE;
}

static void
sync_opacity_scale (PaintWindow *self)
{
    if (!self->opacity_scale || !self->document) return;
    int a = document_active_layer (self->document);
    Layer *l = document_layer_at (self->document, a);
    self->updating_ui = TRUE;
    gtk_range_set_value (GTK_RANGE (self->opacity_scale),
                         l ? l->opacity * 100.0 : 100.0);
    gtk_widget_set_sensitive (self->opacity_scale, l != NULL);
    self->updating_ui = FALSE;
}

static void rebuild_layer_panel (PaintWindow *self);

static void
on_visibility_toggled (GtkCheckButton *check, gpointer ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    if (self->updating_ui || !self->document) return;
    int idx = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (check), "layer-index"));
    document_layer_set_visible (self->document, idx,
                                gtk_check_button_get_active (check));
}

static void
on_row_selected (GtkListBox *list, GtkListBoxRow *row, gpointer ud)
{
    (void)list;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (self->updating_ui || !row || !self->document) return;
    int row_idx = gtk_list_box_row_get_index (row);
    int layer_idx = document_layer_count (self->document) - 1 - row_idx;
    document_set_active_layer (self->document, layer_idx);
}

static void
rebuild_layer_panel (PaintWindow *self)
{
    if (!self->layer_list || !self->document) return;
    self->updating_ui = TRUE;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child (self->layer_list)))
        gtk_list_box_remove (GTK_LIST_BOX (self->layer_list), child);

    int n = document_layer_count (self->document);
    for (int i = n - 1; i >= 0; i--) {
        Layer *l = document_layer_at (self->document, i);
        GtkWidget *row = gtk_list_box_row_new ();
        GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start (box, 6);
        gtk_widget_set_margin_end   (box, 6);
        gtk_widget_set_margin_top   (box, 4);
        gtk_widget_set_margin_bottom(box, 4);

        GtkWidget *check = gtk_check_button_new ();
        gtk_check_button_set_active (GTK_CHECK_BUTTON (check), l->visible);
        g_object_set_data (G_OBJECT (check), "layer-index", GINT_TO_POINTER (i));
        g_signal_connect (check, "toggled",
                          G_CALLBACK (on_visibility_toggled), self);
        gtk_box_append (GTK_BOX (box), check);

        GtkWidget *label = gtk_label_new (l->name);
        gtk_label_set_xalign (GTK_LABEL (label), 0.0f);
        gtk_widget_set_hexpand (label, TRUE);
        gtk_box_append (GTK_BOX (box), label);

        gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), box);
        gtk_list_box_append (GTK_LIST_BOX (self->layer_list), row);
    }
    int a = document_active_layer (self->document);
    if (a >= 0) {
        GtkListBoxRow *r = gtk_list_box_get_row_at_index (
            GTK_LIST_BOX (self->layer_list), n - 1 - a);
        if (r) gtk_list_box_select_row (GTK_LIST_BOX (self->layer_list), r);
    }
    self->updating_ui = FALSE;
    update_layer_actions (self);
    sync_opacity_scale (self);
}

static void
on_doc_composite_changed (void *ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    update_title (self);
    update_status_bar (self);
}

static void
on_doc_layer_list_changed (void *ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    rebuild_layer_panel (self);
    update_title (self);
    update_status_bar (self);
}

static void
on_doc_active_layer_changed (void *ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->document) return;
    self->updating_ui = TRUE;
    int n = document_layer_count (self->document);
    int a = document_active_layer (self->document);
    if (a >= 0) {
        GtkListBoxRow *r = gtk_list_box_get_row_at_index (
            GTK_LIST_BOX (self->layer_list), n - 1 - a);
        if (r) gtk_list_box_select_row (GTK_LIST_BOX (self->layer_list), r);
    }
    self->updating_ui = FALSE;
    update_layer_actions (self);
    sync_opacity_scale (self);
    update_status_bar (self);
}

static const DocumentObserver win_observer = {
    .on_composite_changed    = on_doc_composite_changed,
    .on_layer_list_changed   = on_doc_layer_list_changed,
    .on_active_layer_changed = on_doc_active_layer_changed,
    .on_selection_changed    = on_doc_selection_changed,
};

static void
paint_window_set_document (PaintWindow *self, Document *doc, const char *path)
{
    paint_canvas_set_document (PAINT_CANVAS (self->canvas), NULL);
    if (self->document && self->doc_obs) {
        document_remove_observer (self->document, self->doc_obs);
        self->doc_obs = NULL;
    }
    if (self->document) document_free (self->document);
    self->document = doc;
    g_free (self->current_path);
    self->current_path = path ? g_strdup (path) : NULL;
    paint_canvas_set_document (PAINT_CANVAS (self->canvas), doc);
    if (self->navigator)
        paint_navigator_set_document (PAINT_NAVIGATOR (self->navigator), doc);
    self->doc_obs = document_add_observer (doc, &win_observer, self);
    LOG_INFO ("document set: %dx%d (%s)",
              document_width (doc), document_height (doc),
              path ? path : "untitled");
}

static void
show_error (PaintWindow *self, const char *title, const char *msg)
{
    GtkAlertDialog *d = gtk_alert_dialog_new ("%s", title);
    gtk_alert_dialog_set_detail (d, msg);
    gtk_alert_dialog_show (d, GTK_WINDOW (self));
}

/* ═══ New dialog ════════════════════════════════════════════════════════ */
static void
on_new_ok_clicked (GtkButton *btn, gpointer ud)
{
    (void)btn;
    GtkWidget *dialog = GTK_WIDGET (ud);
    int w = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (
        g_object_get_data (G_OBJECT (dialog), "width-spin")));
    int h = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (
        g_object_get_data (G_OBJECT (dialog), "height-spin")));
    PaintWindow *self = g_object_get_data (G_OBJECT (dialog), "self");

    Document *doc = document_new (w, h);
    Layer *bg = document_add_layer (doc, "Background");
    pixel_buffer_fill_rect (bg->buffer, 0, 0, w, h,
        (Pixel){ .b = 255, .g = 255, .r = 255, .a = 255 });
    document_recomposite_all (doc);
    document_mark_clean (doc);
    paint_window_set_document (self, doc, NULL);
    gtk_window_destroy (GTK_WINDOW (dialog));
}

static void
do_new_dialog (PaintWindow *self)
{
    GtkWidget *dialog = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (dialog), "New Document");
    gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (self));
    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
    gtk_window_set_default_size (GTK_WINDOW (dialog), 320, -1);

    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start (vbox, 12);
    gtk_widget_set_margin_end   (vbox, 12);
    gtk_widget_set_margin_top   (vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);

    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing    (GTK_GRID (grid), 6);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);

    GtkWidget *lw = gtk_label_new ("Width:");
    gtk_widget_set_halign (lw, GTK_ALIGN_END);
    GtkWidget *sw = gtk_spin_button_new_with_range (16, 8192, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (sw), 512);

    GtkWidget *lh = gtk_label_new ("Height:");
    gtk_widget_set_halign (lh, GTK_ALIGN_END);
    GtkWidget *sh = gtk_spin_button_new_with_range (16, 8192, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (sh), 512);

    gtk_grid_attach (GTK_GRID (grid), lw, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), sw, 1, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), lh, 0, 1, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), sh, 1, 1, 1, 1);
    gtk_box_append (GTK_BOX (vbox), grid);

    GtkWidget *btnbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign (btnbox, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label ("Cancel");
    GtkWidget *ok     = gtk_button_new_with_label ("Create");
    gtk_box_append (GTK_BOX (btnbox), cancel);
    gtk_box_append (GTK_BOX (btnbox), ok);
    gtk_box_append (GTK_BOX (vbox), btnbox);

    g_object_set_data (G_OBJECT (dialog), "width-spin",  sw);
    g_object_set_data (G_OBJECT (dialog), "height-spin", sh);
    g_object_set_data (G_OBJECT (dialog), "self", self);

    g_signal_connect_swapped (cancel, "clicked",
        G_CALLBACK (gtk_window_destroy), dialog);
    g_signal_connect (ok, "clicked", G_CALLBACK (on_new_ok_clicked), dialog);

    gtk_window_set_child (GTK_WINDOW (dialog), vbox);
    gtk_window_present (GTK_WINDOW (dialog));
}

/* ═══ Open / Save ═══════════════════════════════════════════════════════ */
static void
on_open_response (GtkNativeDialog *dialog, int response, gpointer ud)
{
    if (response == GTK_RESPONSE_ACCEPT) {
        PaintWindow *self = PAINT_WINDOW (ud);
        GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
        char *path = g_file_get_path (file);
        GError *err = NULL;
        Document *doc = file_io_open_png (path, &err);
        if (doc) paint_window_set_document (self, doc, path);
        else { show_error (self, "Open failed", err ? err->message : "?"); if (err) g_error_free (err); }
        g_free (path); g_object_unref (file);
    }
    g_object_unref (dialog);
}

static void
do_open_dialog (PaintWindow *self)
{
    GtkFileChooserNative *d = gtk_file_chooser_native_new (
        "Open Image", GTK_WINDOW (self),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
    GtkFileFilter *f = gtk_file_filter_new ();
    gtk_file_filter_set_name (f, "Images (*.png)");
    gtk_file_filter_add_pattern (f, "*.png");
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (d), f);
    g_signal_connect (d, "response", G_CALLBACK (on_open_response), self);
    gtk_native_dialog_show (GTK_NATIVE_DIALOG (d));
}

static void
do_save_to_path (PaintWindow *self, const char *path)
{
    char *p = g_strdup (path);
    if (!g_str_has_suffix (p, ".png")) { char *n = g_strconcat (p, ".png", NULL); g_free (p); p = n; }
    GError *err = NULL;
    if (file_io_save_png (self->document, p, &err)) {
        g_free (self->current_path);
        self->current_path = g_strdup (p);
        document_mark_clean (self->document);
        update_title (self);
        LOG_INFO ("saved: %s", p);
    } else {
        show_error (self, "Save failed", err ? err->message : "?");
        if (err) g_error_free (err);
    }
    g_free (p);
}

static void
on_save_response (GtkNativeDialog *dialog, int response, gpointer ud)
{
    if (response == GTK_RESPONSE_ACCEPT) {
        PaintWindow *self = PAINT_WINDOW (ud);
        GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
        char *path = g_file_get_path (file);
        do_save_to_path (self, path);
        g_free (path); g_object_unref (file);
    }
    g_object_unref (dialog);
}

static void
do_save_as_dialog (PaintWindow *self)
{
    GtkFileChooserNative *d = gtk_file_chooser_native_new (
        "Save As", GTK_WINDOW (self),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
    gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (d), "untitled.png");
    GtkFileFilter *f = gtk_file_filter_new ();
    gtk_file_filter_set_name (f, "PNG image");
    gtk_file_filter_add_pattern (f, "*.png");
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (d), f);
    g_signal_connect (d, "response", G_CALLBACK (on_save_response), self);
    gtk_native_dialog_show (GTK_NATIVE_DIALOG (d));
}

/* ═══ Actions ═══════════════════════════════════════════════════════════ */
static void action_new     (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_open    (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_save    (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_save_as (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_add    (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_delete (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_up     (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_down   (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_merge  (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_about        (GSimpleAction *a, GVariant *p, gpointer ud);

static void action_new (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; do_new_dialog (PAINT_WINDOW (ud)); }
static void action_open (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; do_open_dialog (PAINT_WINDOW (ud)); }
static void action_save (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (self->current_path) do_save_to_path (self, self->current_path);
    else                    do_save_as_dialog (self);
}
static void action_save_as (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; do_save_as_dialog (PAINT_WINDOW (ud)); }
static void action_layer_add (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    char *n = g_strdup_printf ("Layer %d", document_layer_count (self->document) + 1);
    document_add_layer (self->document, n);
    g_free (n);
}
static void action_layer_delete (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; PaintWindow *s = PAINT_WINDOW (ud);
  document_remove_layer (s->document, document_active_layer (s->document)); }
static void action_layer_up (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; PaintWindow *s = PAINT_WINDOW (ud);
  int i = document_active_layer (s->document);
  document_move_layer (s->document, i, i + 1); }
static void action_layer_down (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; PaintWindow *s = PAINT_WINDOW (ud);
  int i = document_active_layer (s->document);
  document_move_layer (s->document, i, i - 1); }
static void action_layer_merge (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; PaintWindow *s = PAINT_WINDOW (ud);
  document_merge_down (s->document, document_active_layer (s->document)); }
static void action_about (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    GtkWidget *d = gtk_about_dialog_new ();
    gtk_about_dialog_set_program_name (GTK_ABOUT_DIALOG (d), "Paint C");
    gtk_about_dialog_set_version      (GTK_ABOUT_DIALOG (d), "0.2.0");
    gtk_about_dialog_set_comments     (GTK_ABOUT_DIALOG (d),
        "A minimal, modular raster paint application.");
    gtk_window_set_transient_for (GTK_WINDOW (d), GTK_WINDOW (self));
    gtk_window_present (GTK_WINDOW (d));
}


static void
action_copy_selection (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->document) return;
    if (!document_has_selection (self->document)) {
        LOG_WARN ("Copy: no selection — select an area first (Ctrl+R)");
        return;
    }
    PixelBuffer *buf = document_copy_region (self->document);
    if (!buf) return;
    clipboard_set (buf);
    clipboard_set_system (buf);
    LOG_INFO ("copied %dx%d", buf->width, buf->height);
    pixel_buffer_free (buf);
}

static void
action_cut_selection (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->document) return;
    if (!document_has_selection (self->document)) { LOG_WARN ("Cut: no selection"); return; }
    PixelBuffer *buf = document_copy_region (self->document);
    if (!buf) return;
    clipboard_set (buf);
    clipboard_set_system (buf);
    pixel_buffer_free (buf);
    document_clear_region (self->document);
    LOG_INFO ("cut");
}

typedef struct { PaintWindow *win; bool as_layer; } SystemPasteCtx;

static void
on_system_paste_complete (PixelBuffer *buf, void *ud)
{
    SystemPasteCtx *ctx = ud;
    if (buf && ctx->win->document) {
        if (ctx->as_layer)
            document_paste_as_layer (ctx->win->document, buf, "Pasted");
        else
            document_paste_buffer (ctx->win->document, buf, 0, 0);
    }
    g_free (ctx);
}

static void
do_paste (PaintWindow *self, bool as_layer)
{
    if (!self->document) return;
    if (clipboard_has_internal ()) {
        PixelBuffer *buf = clipboard_get_internal ();
        if (buf) {
            if (as_layer) document_paste_as_layer (self->document, buf, "Pasted");
            else          document_paste_buffer (self->document, buf, 0, 0);
            pixel_buffer_free (buf);
            return;
        }
    }
    /* Fall back to system clipboard */
    SystemPasteCtx *ctx = g_new0 (SystemPasteCtx, 1);
    ctx->win = self;
    ctx->as_layer = as_layer;
    clipboard_get_system_async (on_system_paste_complete, ctx);
}

static void action_paste       (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; do_paste (PAINT_WINDOW (ud), false); }
static void action_paste_layer (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; do_paste (PAINT_WINDOW (ud), true); }

static void
action_select_all (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->document) return;
    document_set_selection (self->document, 0, 0,
        document_width (self->document), document_height (self->document));
}

static void
action_deselect (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->document) return;
    if (!document_has_selection (self->document)) return;
    document_clear_selection (self->document);
}

static void
action_delete_sel (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->document) return;
    if (!document_has_selection (self->document)) return;
    document_clear_region (self->document);
}

static void
action_zoom_in (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    paint_canvas_zoom_in (PAINT_CANVAS (PAINT_WINDOW (ud)->canvas));
}
static void
action_zoom_out (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    paint_canvas_zoom_out (PAINT_CANVAS (PAINT_WINDOW (ud)->canvas));
}
static void
action_zoom_100 (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    paint_canvas_set_zoom (PAINT_CANVAS (PAINT_WINDOW (ud)->canvas), 1.0);
}
static void
action_zoom_fit (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a; (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->hadj || !self->vadj) return;
    int vw = (int)gtk_adjustment_get_page_size (self->hadj);
    int vh = (int)gtk_adjustment_get_page_size (self->vadj);
    paint_canvas_zoom_fit (PAINT_CANVAS (self->canvas), vw, vh);
}

static void
activate_toggle (GtkWidget *tb)
{
    if (tb) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (tb), TRUE);
}

static void action_tool_pencil     (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; activate_toggle (PAINT_WINDOW (ud)->tb_pencil); }
static void action_tool_eraser     (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; activate_toggle (PAINT_WINDOW (ud)->tb_eraser); }
static void action_tool_eyedropper (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; activate_toggle (PAINT_WINDOW (ud)->tb_eyedropper); }
static void action_tool_blur       (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; activate_toggle (PAINT_WINDOW (ud)->tb_blur); }
static void action_tool_select     (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a;(void)p; activate_toggle (PAINT_WINDOW (ud)->tb_select); }

static void
action_undo (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; (void)ud; LOG_WARN ("Undo not implemented yet (TODO M8)"); }
static void
action_redo (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; (void)ud; LOG_WARN ("Redo not implemented yet (TODO M8)"); }

static const GActionEntry win_actions[] = {
    { "new",              action_new,          NULL, NULL, NULL, {0} },
    { "open",             action_open,         NULL, NULL, NULL, {0} },
    { "save",             action_save,         NULL, NULL, NULL, {0} },
    { "save-as",          action_save_as,      NULL, NULL, NULL, {0} },
    { "layer-add",        action_layer_add,    NULL, NULL, NULL, {0} },
    { "layer-delete",     action_layer_delete, NULL, NULL, NULL, {0} },
    { "layer-up",         action_layer_up,     NULL, NULL, NULL, {0} },
    { "layer-down",       action_layer_down,   NULL, NULL, NULL, {0} },
    { "layer-merge-down", action_layer_merge,  NULL, NULL, NULL, {0} },
    { "cut",              action_cut_selection,NULL, NULL, NULL, {0} },
    { "copy",             action_copy_selection,NULL, NULL, NULL, {0} },
    { "paste",            action_paste,        NULL, NULL, NULL, {0} },
    { "paste-layer",      action_paste_layer,  NULL, NULL, NULL, {0} },
    { "select-all",       action_select_all,   NULL, NULL, NULL, {0} },
    { "deselect",         action_deselect,     NULL, NULL, NULL, {0} },
    { "delete-sel",       action_delete_sel,   NULL, NULL, NULL, {0} },
    { "undo",             action_undo,         NULL, NULL, NULL, {0} },
    { "redo",             action_redo,         NULL, NULL, NULL, {0} },
    { "tool-pencil",      action_tool_pencil,  NULL, NULL, NULL, {0} },
    { "tool-eraser",      action_tool_eraser,  NULL, NULL, NULL, {0} },
    { "tool-eyedropper",  action_tool_eyedropper,NULL, NULL, NULL, {0} },
    { "tool-blur",        action_tool_blur,    NULL, NULL, NULL, {0} },
    { "tool-select",      action_tool_select,  NULL, NULL, NULL, {0} },
    { "zoom-in",          action_zoom_in,      NULL, NULL, NULL, {0} },
    { "zoom-out",         action_zoom_out,     NULL, NULL, NULL, {0} },
    { "zoom-fit",         action_zoom_fit,     NULL, NULL, NULL, {0} },
    { "zoom-100",         action_zoom_100,     NULL, NULL, NULL, {0} },
    { "preferences",      action_preferences,  NULL, NULL, NULL, {0} },
    { "toggle-panel",     action_toggle_panel, "s", NULL, NULL, {0} },
    { "about",            action_about,        NULL, NULL, NULL, {0} },
};

/* Button wrappers — avoid the swapped-signature crash from before. */
static void on_layer_add_clicked    (GtkButton *b, gpointer ud) { (void)b; action_layer_add   (NULL, NULL, ud); }
static void on_layer_delete_clicked (GtkButton *b, gpointer ud) { (void)b; action_layer_delete (NULL, NULL, ud); }
static void on_layer_up_clicked     (GtkButton *b, gpointer ud) { (void)b; action_layer_up     (NULL, NULL, ud); }
static void on_layer_down_clicked   (GtkButton *b, gpointer ud) { (void)b; action_layer_down   (NULL, NULL, ud); }
static void on_layer_merge_clicked  (GtkButton *b, gpointer ud) { (void)b; action_layer_merge  (NULL, NULL, ud); }

/* ═══ Widget callbacks ══════════════════════════════════════════════════ */
static void
set_pencil_color_from_rgba (PaintWindow *self, const GdkRGBA *rgba)
{
    Pixel p = {
        .r = (uint8_t)(rgba->red   * 255.0 + 0.5),
        .g = (uint8_t)(rgba->green * 255.0 + 0.5),
        .b = (uint8_t)(rgba->blue  * 255.0 + 0.5),
        .a = (uint8_t)(rgba->alpha * 255.0 + 0.5),
    };
    pencil_tool_set_color (self->pencil, p);
}

static void
on_color_set (GtkColorDialogButton *btn, GParamSpec *pspec, gpointer ud)
{
    (void)pspec;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (self->updating_ui) return;
    const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba (btn);
    if (rgba) set_pencil_color_from_rgba (self, rgba);
}

static void
on_eyedropper_picked (Pixel color, void *ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    GdkRGBA rgba = {
        color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0
    };
    self->updating_ui = TRUE;
    gtk_color_dialog_button_set_rgba (GTK_COLOR_DIALOG_BUTTON (self->color_button), &rgba);
    self->updating_ui = FALSE;
    set_pencil_color_from_rgba (self, &rgba);
    /* Auto-switch back to pencil for the classic paint.net behaviour. */
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->tb_pencil), TRUE);
}

static void
on_radius_changed (GtkSpinButton *spin, gpointer ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    if (self->updating_ui) return;
    int r = gtk_spin_button_get_value_as_int (spin);
    Tool *t = tool_engine_get_active ();
    if      (t == self->pencil) pencil_tool_set_radius (t, r);
    else if (t == self->eraser) eraser_tool_set_radius (t, r);
    else if (t == self->blur)   blur_tool_set_radius (t, r);
}

static void
on_opacity_changed (GtkRange *range, gpointer ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    if (self->updating_ui || !self->document) return;
    int a = document_active_layer (self->document);
    document_layer_set_opacity (self->document, a,
                                (float)(gtk_range_get_value (range) / 100.0));
}

static void
on_cursor_moved (PaintCanvas *canvas, double x, double y, gpointer ud)
{
    (void)canvas;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->st_cursor) return;
    char buf[64];
    snprintf (buf, sizeof buf, "Pos: (%.0f, %.0f)", x, y);
    gtk_label_set_text (GTK_LABEL (self->st_cursor), buf);
}

static void
on_zoom_changed (PaintCanvas *canvas, double zoom, gpointer ud)
{
    (void)canvas;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (self->st_zoom) {
        char buf[32];
        snprintf (buf, sizeof buf, "%.0f%%", zoom * 100.0);
        gtk_label_set_text (GTK_LABEL (self->st_zoom), buf);
    }
    if (self->navigator && self->hadj && self->vadj)
        paint_navigator_set_scroll (PAINT_NAVIGATOR (self->navigator),
                                    self->hadj, self->vadj, zoom);
}

static void
on_tool_toggled (GtkToggleButton *btn, gpointer ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    if (self->updating_ui) return;
    if (!gtk_toggle_button_get_active (btn)) return;
    const char *id = g_object_get_data (G_OBJECT (btn), "tool-id");
    self->updating_ui = TRUE;
    Tool *picked = self->pencil;
    if      (g_strcmp0 (id, "pencil") == 0)     picked = self->pencil;
    else if (g_strcmp0 (id, "eraser") == 0)     picked = self->eraser;
    else if (g_strcmp0 (id, "eyedropper") == 0) picked = self->eyedropper;
    else if (g_strcmp0 (id, "blur") == 0)       picked = self->blur;
    else if (g_strcmp0 (id, "select") == 0)     picked = self->select;
    else {
        /* Plugin tool: "plugin:<index>" */
        int idx = 0;
        if (sscanf (id, "plugin:%d", &idx) == 1 && self->plugin_tools)
            if ((guint)idx < self->plugin_tools->len)
                picked = g_ptr_array_index (self->plugin_tools, idx);
    }
    /* Toggle off other buttons. */
    GtkWidget *all[] = { self->tb_pencil, self->tb_eraser,
                         self->tb_eyedropper, self->tb_blur, self->tb_select };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
        if (all[i] && all[i] != GTK_WIDGET (btn))
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (all[i]), FALSE);
    tool_engine_set_active (picked);
    self->updating_ui = FALSE;
    sync_radius_spin (self);
    update_status_bar (self);
    LOG_DEBUG ("tool: %s", id);
}

/* ═══ Status bar ════════════════════════════════════════════════════════ */
static void
update_status_bar (PaintWindow *self)
{
    if (!self->document) return;
    char buf[128];

    snprintf (buf, sizeof buf, "%d × %d",
              document_width (self->document), document_height (self->document));
    if (self->st_dim) gtk_label_set_text (GTK_LABEL (self->st_dim), buf);

    Tool *t = tool_engine_get_active ();
    if (self->st_hint)
        gtk_label_set_text (GTK_LABEL (self->st_hint),
                            tool_hint (t ? t->vtable->id : NULL));
}

/* ═══ Menu bar ══════════════════════════════════════════════════════════ */
static GtkWidget *
build_menubar (void)
{
    GMenu *file = g_menu_new ();
    g_menu_append (file, "New…",     "win.new");
    g_menu_append (file, "Open…",    "win.open");
    g_menu_append (file, "Save",     "win.save");
    g_menu_append (file, "Save As…", "win.save-as");
    { GMenu *s = g_menu_new (); g_menu_append (s, "Quit", "app.quit");
      g_menu_append_section (file, NULL, G_MENU_MODEL (s)); }

    GMenu *edit = g_menu_new ();
    g_menu_append (edit, "Undo", "win.undo");
    g_menu_append (edit, "Redo", "win.redo");
    { GMenu *s = g_menu_new ();
      g_menu_append (s, "Cut",       "win.cut");
      g_menu_append (s, "Copy",      "win.copy");
      g_menu_append (s, "Paste",     "win.paste");
      g_menu_append (s, "Paste as New Layer", "win.paste-layer");
      g_menu_append_section (edit, NULL, G_MENU_MODEL (s)); }
    { GMenu *s = g_menu_new ();
      g_menu_append (s, "Select All", "win.select-all");
      g_menu_append (s, "Deselect",   "win.deselect");
      g_menu_append (s, "Delete Selection", "win.delete-sel");
      g_menu_append_section (edit, NULL, G_MENU_MODEL (s)); }
    g_menu_append (edit, "Preferences…", "win.preferences");

    GMenu *tools = g_menu_new ();
    g_menu_append (tools, "Pencil",          "win.tool-pencil");
    g_menu_append (tools, "Eraser",          "win.tool-eraser");
    g_menu_append (tools, "Eyedropper",      "win.tool-eyedropper");
    g_menu_append (tools, "Blur",            "win.tool-blur");
    g_menu_append (tools, "Select Rectangle","win.tool-select");

    GMenu *layer = g_menu_new ();
    g_menu_append (layer, "New Layer",    "win.layer-add");
    g_menu_append (layer, "Delete Layer", "win.layer-delete");
    { GMenu *s = g_menu_new ();
      g_menu_append (s, "Move Up",    "win.layer-up");
      g_menu_append (s, "Move Down",  "win.layer-down");
      g_menu_append (s, "Merge Down", "win.layer-merge-down");
      g_menu_append_section (layer, NULL, G_MENU_MODEL (s)); }

    GMenu *view = g_menu_new ();
    g_menu_append (view, "Zoom In",         "win.zoom-in");
    g_menu_append (view, "Zoom Out",        "win.zoom-out");
    g_menu_append (view, "Fit to Window",   "win.zoom-fit");
    g_menu_append (view, "Actual Size (100%)","win.zoom-100");
    { GMenu *s = g_menu_new ();
      g_menu_append (s, "Tools Panel",   "win.toggle-panel('tools')");
      g_menu_append (s, "Colors Panel",  "win.toggle-panel('colors')");
      g_menu_append (s, "History Panel", "win.toggle-panel('history')");
      g_menu_append (s, "Layers Panel",  "win.toggle-panel('layers')");
      g_menu_append_section (view, NULL, G_MENU_MODEL (s)); }

    GMenu *help = g_menu_new ();
    g_menu_append (help, "About Paint", "win.about");

    GMenu *bar = g_menu_new ();
    g_menu_append_submenu (bar, "File",  G_MENU_MODEL (file));
    g_menu_append_submenu (bar, "Edit",  G_MENU_MODEL (edit));
    g_menu_append_submenu (bar, "Tools", G_MENU_MODEL (tools));
    g_menu_append_submenu (bar, "Layer", G_MENU_MODEL (layer));
    g_menu_append_submenu (bar, "View",  G_MENU_MODEL (view));
    g_menu_append_submenu (bar, "Help",  G_MENU_MODEL (help));
    return gtk_popover_menu_bar_new_from_model (G_MENU_MODEL (bar));
}

/* ═══ Toolbar ═══════════════════════════════════════════════════════════ */
static GtkWidget *
make_tool_toggle (PaintWindow *self, const char *id, const char *label)
{
    GtkWidget *b = gtk_toggle_button_new_with_label (label);
    g_object_set_data_full (G_OBJECT (b), "tool-id", g_strdup (id), g_free);
    g_signal_connect (b, "toggled", G_CALLBACK (on_tool_toggled), self);
    return b;
}

static void on_zoom_fit_clicked (GtkButton *b, gpointer ud);

static GtkWidget *
build_toolbar (PaintWindow *self)
{
    GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start (bar, 6);
    gtk_widget_set_margin_end   (bar, 6);
    gtk_widget_set_margin_top   (bar, 4);
    gtk_widget_set_margin_bottom(bar, 4);

    self->tb_pencil     = make_tool_toggle (self, "pencil",     "Pencil");
    self->tb_eraser     = make_tool_toggle (self, "eraser",     "Eraser");
    self->tb_eyedropper = make_tool_toggle (self, "eyedropper", "Eyedropper");
    self->tb_blur       = make_tool_toggle (self, "blur",       "Blur");
    self->tb_select     = make_tool_toggle (self, "select",     "Select");

    gtk_box_append (GTK_BOX (bar), self->tb_pencil);
    gtk_box_append (GTK_BOX (bar), self->tb_eraser);
    gtk_box_append (GTK_BOX (bar), self->tb_eyedropper);
    gtk_box_append (GTK_BOX (bar), self->tb_blur);

    /* Plugin tools — one toggle each. */
    if (self->plugin_tools) {
        for (guint i = 0; i < self->plugin_tools->len; i++) {
            Tool *t = g_ptr_array_index (self->plugin_tools, i);
            if (!t || !t->vtable) continue;
            char *id = g_strdup_printf ("plugin:%u", i);
            GtkWidget *b = make_tool_toggle (self, id, t->vtable->name);
            gtk_box_append (GTK_BOX (bar), b);
            g_free (id);
        }
    }
    gtk_box_append (GTK_BOX (bar), gtk_separator_new (GTK_ORIENTATION_VERTICAL));

    /* Color button — GTK 4.10+ API, no deprecation. */
    GtkWidget *lbl_color = gtk_label_new ("Color:");
    GtkColorDialog *cd = gtk_color_dialog_new ();
    self->color_button = gtk_color_dialog_button_new (cd);
    const GdkRGBA black = { 0, 0, 0, 1 };
    gtk_color_dialog_button_set_rgba (GTK_COLOR_DIALOG_BUTTON (self->color_button), &black);
    g_signal_connect (self->color_button, "notify::rgba",
                      G_CALLBACK (on_color_set), self);
    gtk_box_append (GTK_BOX (bar), lbl_color);
    gtk_box_append (GTK_BOX (bar), self->color_button);
    gtk_box_append (GTK_BOX (bar), gtk_separator_new (GTK_ORIENTATION_VERTICAL));

    /* Radius. */
    GtkWidget *lbl_r = gtk_label_new ("Size:");
    self->radius_spin = gtk_spin_button_new_with_range (1, 200, 1);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->radius_spin), 2);
    g_signal_connect (self->radius_spin, "value-changed",
                      G_CALLBACK (on_radius_changed), self);
    gtk_box_append (GTK_BOX (bar), lbl_r);
    gtk_box_append (GTK_BOX (bar), self->radius_spin);
    gtk_box_append (GTK_BOX (bar), gtk_separator_new (GTK_ORIENTATION_VERTICAL));

    /* Zoom. */
    GtkWidget *b_zo = gtk_button_new_with_label ("−");
    GtkWidget *b_zi = gtk_button_new_with_label ("+");
    GtkWidget *b_zf = gtk_button_new_with_label ("Fit");
    g_signal_connect_swapped (b_zo, "clicked",
        G_CALLBACK (paint_canvas_zoom_out), self->canvas);
    g_signal_connect_swapped (b_zi, "clicked",
        G_CALLBACK (paint_canvas_zoom_in), self->canvas);
    g_signal_connect (b_zf, "clicked", G_CALLBACK (on_zoom_fit_clicked), self);
    /* Fit needs args — use a dedicated callback. */
    GtkWidget *lbl_z = gtk_label_new ("Zoom:");
    gtk_box_append (GTK_BOX (bar), lbl_z);
    gtk_box_append (GTK_BOX (bar), b_zo);
    gtk_box_append (GTK_BOX (bar), b_zi);
    gtk_box_append (GTK_BOX (bar), b_zf);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (self->tb_pencil), TRUE);
    return bar;
}

/* Fit needs the viewport size — dedicated callback. */
static void
on_zoom_fit_clicked (GtkButton *b, gpointer ud)
{
    (void)b;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->hadj || !self->vadj) return;
    int vw = (int)gtk_adjustment_get_page_size (self->hadj);
    int vh = (int)gtk_adjustment_get_page_size (self->vadj);
    paint_canvas_zoom_fit (PAINT_CANVAS (self->canvas), vw, vh);
}

/* ═══ Layer panel ═══════════════════════════════════════════════════════ */

/* ═══ Init / finalize ═══════════════════════════════════════════════════ */

/* ── Config load / save ────────────────────────────────────────────── */
static void
config_load_window_state (PaintWindow *self)
{
    int w = config_get_int ("window/width", 1200);
    int h = config_get_int ("window/height", 800);
    gtk_window_set_default_size (GTK_WINDOW (self), w, h);

    int pr = config_get_int ("tool/pencil/radius", 2);
    int er = config_get_int ("tool/eraser/radius", 8);
    int br = config_get_int ("tool/blur/radius", 6);
    pencil_tool_set_radius (self->pencil, pr);
    eraser_tool_set_radius (self->eraser, er);
    blur_tool_set_radius   (self->blur,   br);

    int r = config_get_int ("color/r", 0);
    int g = config_get_int ("color/g", 0);
    int b = config_get_int ("color/b", 0);
    Pixel c = { (uint8_t)b, (uint8_t)g, (uint8_t)r, 255 };
    pencil_tool_set_color (self->pencil, c);

    self->panel_tools_visible   = config_get_bool ("panel/tools",   true);
    self->panel_colors_visible  = config_get_bool ("panel/colors",  true);
    self->panel_history_visible = config_get_bool ("panel/history", true);
    self->panel_layers_visible  = config_get_bool ("panel/layers",  true);

    LOG_INFO ("config: %dx%d pencil_r=%d eraser_r=%d blur_r=%d color=#%02x%02x%02x",
              w, h, pr, er, br, r, g, b);
}

static void
config_save_window_state (PaintWindow *self)
{
    if (!self) return;
    int w = gtk_widget_get_width  (GTK_WIDGET (self));
    int h = gtk_widget_get_height (GTK_WIDGET (self));
    if (w > 0 && h > 0) {
        config_set_int ("window/width",  w);
        config_set_int ("window/height", h);
    }
    if (self->pencil) config_set_int ("tool/pencil/radius", pencil_tool_get_radius (self->pencil));
    if (self->eraser) config_set_int ("tool/eraser/radius", eraser_tool_get_radius (self->eraser));
    if (self->blur)   config_set_int ("tool/blur/radius",   blur_tool_get_radius   (self->blur));

    /* Pull current colour back out of the button */
    if (self->color_button) {
        const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba (
            GTK_COLOR_DIALOG_BUTTON (self->color_button));
        if (rgba) {
            config_set_int ("color/r", (int)(rgba->red   * 255 + 0.5));
            config_set_int ("color/g", (int)(rgba->green * 255 + 0.5));
            config_set_int ("color/b", (int)(rgba->blue  * 255 + 0.5));
        }
    }

    if (self->panel_tools)   config_set_bool ("panel/tools",   gtk_widget_get_visible (self->panel_tools));
    if (self->panel_colors)  config_set_bool ("panel/colors",  gtk_widget_get_visible (self->panel_colors));
    if (self->panel_history) config_set_bool ("panel/history", gtk_widget_get_visible (self->panel_history));
    if (self->panel_layers)  config_set_bool ("panel/layers",  gtk_widget_get_visible (self->panel_layers));

    config_save ();
}

/* ── Hover dim on floating panels ──────────────────────────────────── */
static void
dim_overlays (PaintWindow *self, bool dim)
{
    float o = dim ? 0.35f : 1.0f;
    GtkWidget *ps[] = { self->panel_tools, self->panel_colors,
                        self->panel_history, self->panel_layers };
    for (size_t i = 0; i < sizeof ps / sizeof ps[0]; i++)
        if (ps[i]) gtk_widget_set_opacity (ps[i], o);
}

static void
on_canvas_enter (GtkEventControllerMotion *ctrl, double x, double y, gpointer ud)
{
    (void)ctrl; (void)x; (void)y;
    dim_overlays (PAINT_WINDOW (ud), true);
}
static void
on_canvas_leave (GtkEventControllerMotion *ctrl, gpointer ud)
{
    (void)ctrl;
    dim_overlays (PAINT_WINDOW (ud), false);
}

/* ── Tool hint text ────────────────────────────────────────────────── */
static const char *
tool_hint (const char *id)
{
    if (!id) return "Pick a tool to begin.";
    if (!g_strcmp0 (id, "pencil"))     return "Click and drag to draw with the pencil.";
    if (!g_strcmp0 (id, "eraser"))     return "Click and drag to erase pixels to transparency.";
    if (!g_strcmp0 (id, "eyedropper")) return "Click to pick the colour under the cursor.";
    if (!g_strcmp0 (id, "blur"))       return "Click and drag to smooth pixels.";
    if (!g_strcmp0 (id, "select"))     return "Click and drag to select. Ctrl+C to copy, Ctrl+V to paste.";
    return "";
}

/* ── Preferences dialog ────────────────────────────────────────────── */
static void on_prefs_ok (GtkButton *b, gpointer ud);

static void
do_preferences_dialog (PaintWindow *self)
{
    GtkWidget *dialog = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (dialog), "Preferences");
    gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (self));
    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
    gtk_window_set_default_size (GTK_WINDOW (dialog), 460, -1);

    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start (vbox, 12);
    gtk_widget_set_margin_end   (vbox, 12);
    gtk_widget_set_margin_top   (vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);

    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing    (GTK_GRID (grid), 8);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);

    int row = 0;
    GtkWidget *l1 = gtk_label_new ("Addons path:");
    gtk_widget_set_halign (l1, GTK_ALIGN_END);
    GtkWidget *e1 = gtk_entry_new ();
    gtk_editable_set_text (GTK_EDITABLE (e1),
        config_get_string ("addons/path", "./build/addons"));
    gtk_widget_set_hexpand (e1, TRUE);
    gtk_grid_attach (GTK_GRID (grid), l1, 0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), e1, 1, row, 1, 1); row++;

    GtkWidget *l2 = gtk_label_new ("Preferences file:");
    gtk_widget_set_halign (l2, GTK_ALIGN_END);
    GtkWidget *p2 = gtk_label_new (config_path ());
    gtk_label_set_selectable (GTK_LABEL (p2), TRUE);
    gtk_widget_set_halign (p2, GTK_ALIGN_START);
    gtk_grid_attach (GTK_GRID (grid), l2, 0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), p2, 1, row, 1, 1); row++;

    GtkWidget *l3 = gtk_label_new ("Panels:");
    gtk_widget_set_halign (l3, GTK_ALIGN_END);
    GtkWidget *pbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *c_tools   = gtk_check_button_new_with_label ("Tools");
    GtkWidget *c_colors  = gtk_check_button_new_with_label ("Colors");
    GtkWidget *c_history = gtk_check_button_new_with_label ("History");
    GtkWidget *c_layers  = gtk_check_button_new_with_label ("Layers");
    gtk_check_button_set_active (GTK_CHECK_BUTTON (c_tools),
        config_get_bool ("panel/tools", true));
    gtk_check_button_set_active (GTK_CHECK_BUTTON (c_colors),
        config_get_bool ("panel/colors", true));
    gtk_check_button_set_active (GTK_CHECK_BUTTON (c_history),
        config_get_bool ("panel/history", true));
    gtk_check_button_set_active (GTK_CHECK_BUTTON (c_layers),
        config_get_bool ("panel/layers", true));
    gtk_box_append (GTK_BOX (pbox), c_tools);
    gtk_box_append (GTK_BOX (pbox), c_colors);
    gtk_box_append (GTK_BOX (pbox), c_history);
    gtk_box_append (GTK_BOX (pbox), c_layers);
    gtk_grid_attach (GTK_GRID (grid), l3,   0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), pbox, 1, row, 1, 1); row++;

    gtk_box_append (GTK_BOX (vbox), grid);

    GtkWidget *btnbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign (btnbox, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label ("Cancel");
    GtkWidget *ok     = gtk_button_new_with_label ("Save");
    gtk_box_append (GTK_BOX (btnbox), cancel);
    gtk_box_append (GTK_BOX (btnbox), ok);
    gtk_box_append (GTK_BOX (vbox), btnbox);

    g_object_set_data (G_OBJECT (dialog), "self",       self);
    g_object_set_data (G_OBJECT (dialog), "addons",     e1);
    g_object_set_data (G_OBJECT (dialog), "c_tools",    c_tools);
    g_object_set_data (G_OBJECT (dialog), "c_colors",   c_colors);
    g_object_set_data (G_OBJECT (dialog), "c_history",  c_history);
    g_object_set_data (G_OBJECT (dialog), "c_layers",   c_layers);

    g_signal_connect_swapped (cancel, "clicked",
        G_CALLBACK (gtk_window_destroy), dialog);
    g_signal_connect (ok, "clicked", G_CALLBACK (on_prefs_ok), dialog);

    gtk_window_set_child (GTK_WINDOW (dialog), vbox);
    gtk_window_present (GTK_WINDOW (dialog));
}

static void
on_prefs_ok (GtkButton *b, gpointer ud)
{
    (void)b;
    GtkWidget *dialog = GTK_WIDGET (ud);
    PaintWindow *self = g_object_get_data (G_OBJECT (dialog), "self");
    GtkWidget *e1     = g_object_get_data (G_OBJECT (dialog), "addons");
    GtkWidget *c_t    = g_object_get_data (G_OBJECT (dialog), "c_tools");
    GtkWidget *c_c    = g_object_get_data (G_OBJECT (dialog), "c_colors");
    GtkWidget *c_h    = g_object_get_data (G_OBJECT (dialog), "c_history");
    GtkWidget *c_l    = g_object_get_data (G_OBJECT (dialog), "c_layers");

    config_set_string ("addons/path",
        gtk_editable_get_text (GTK_EDITABLE (e1)));
    config_set_bool ("panel/tools",
        gtk_check_button_get_active (GTK_CHECK_BUTTON (c_t)));
    config_set_bool ("panel/colors",
        gtk_check_button_get_active (GTK_CHECK_BUTTON (c_c)));
    config_set_bool ("panel/history",
        gtk_check_button_get_active (GTK_CHECK_BUTTON (c_h)));
    config_set_bool ("panel/layers",
        gtk_check_button_get_active (GTK_CHECK_BUTTON (c_l)));

    if (self->panel_tools)   gtk_widget_set_visible (self->panel_tools,   gtk_check_button_get_active (GTK_CHECK_BUTTON (c_t)));
    if (self->panel_colors)  gtk_widget_set_visible (self->panel_colors,  gtk_check_button_get_active (GTK_CHECK_BUTTON (c_c)));
    if (self->panel_history) gtk_widget_set_visible (self->panel_history, gtk_check_button_get_active (GTK_CHECK_BUTTON (c_h)));
    if (self->panel_layers)  gtk_widget_set_visible (self->panel_layers,  gtk_check_button_get_active (GTK_CHECK_BUTTON (c_l)));

    config_save ();
    LOG_INFO ("preferences saved");
    gtk_window_destroy (GTK_WINDOW (dialog));
}

static void
action_preferences (GSimpleAction *a, GVariant *p, gpointer ud)
{ (void)a; (void)p; do_preferences_dialog (PAINT_WINDOW (ud)); }

static void
action_toggle_panel (GSimpleAction *a, GVariant *p, gpointer ud)
{
    (void)a;
    const char *name = g_variant_get_string (p, NULL);
    PaintWindow *self = PAINT_WINDOW (ud);
    GtkWidget *target = NULL;
    if      (!g_strcmp0 (name, "tools"))   target = self->panel_tools;
    else if (!g_strcmp0 (name, "colors"))  target = self->panel_colors;
    else if (!g_strcmp0 (name, "history")) target = self->panel_history;
    else if (!g_strcmp0 (name, "layers"))  target = self->panel_layers;
    if (target) gtk_widget_set_visible (target,
        !gtk_widget_get_visible (target));
}


static void on_btn_prefs   (GtkButton *b, gpointer ud) { (void)b; action_preferences (NULL, NULL, ud); }
static void on_btn_about   (GtkButton *b, gpointer ud) { (void)b; action_about       (NULL, NULL, ud); }
static void on_btn_zoom_in (GtkButton *b, gpointer ud) { (void)b; action_zoom_in     (NULL, NULL, ud); }
static void on_btn_zoom_out(GtkButton *b, gpointer ud) { (void)b; action_zoom_out    (NULL, NULL, ud); }
static void on_btn_zoom_fit(GtkButton *b, gpointer ud) { (void)b; action_zoom_fit    (NULL, NULL, ud); }


static void
on_pick_color (PaintCanvas *canvas, double ix, double iy, gpointer ud)
{
    (void)canvas;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->document || !self->color_button) return;
    int x = (int)ix, y = (int)iy;
    int w = document_width (self->document);
    int h = document_height (self->document);
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    const uint8_t *comp = document_composite (self->document);
    if (!comp) return;
    const Pixel *p = (const Pixel *)(comp + ((gsize)y * w + x) * 4);
    GdkRGBA rgba = { p->r / 255.0, p->g / 255.0, p->b / 255.0, p->a / 255.0 };
    self->updating_ui = TRUE;
    gtk_color_dialog_button_set_rgba (
        GTK_COLOR_DIALOG_BUTTON (self->color_button), &rgba);
    self->updating_ui = FALSE;
    set_pencil_color_from_rgba (self, &rgba);
    LOG_INFO ("picked colour at (%d,%d): #%02x%02x%02x", x, y, p->r, p->g, p->b);
}


static void
update_cursor_radius (PaintWindow *self)
{
    if (!self->canvas) return;
    Tool *t = tool_engine_get_active ();
    double r = 0;
    if      (t == self->pencil) r = pencil_tool_get_radius (t);
    else if (t == self->eraser) r = eraser_tool_get_radius (t);
    else if (t == self->blur)   r = blur_tool_get_radius   (t);
    paint_canvas_set_cursor_radius (PAINT_CANVAS (self->canvas), r);
}






static void
tool_options_refresh (PaintWindow *self)
{
    if (!self->tool_options_box) return;
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child (self->tool_options_box)))
        gtk_box_remove (GTK_BOX (self->tool_options_box), child);

    Tool *t = tool_engine_get_active ();
    const char *name = t ? t->vtable->name : "None";
    GtkWidget *lbl = gtk_label_new (NULL);
    char *markup = g_markup_printf_escaped ("<b>%s</b>", name);
    gtk_label_set_markup (GTK_LABEL (lbl), markup);
    g_free (markup);
    gtk_box_append (GTK_BOX (self->tool_options_box), lbl);

    int radius = 2;
    if      (t == self->pencil) radius = pencil_tool_get_radius (t);
    else if (t == self->eraser) radius = eraser_tool_get_radius (t);
    else if (t == self->blur)   radius = blur_tool_get_radius   (t);

    if (t == self->pencil || t == self->eraser || t == self->blur) {
        gtk_box_append (GTK_BOX (self->tool_options_box),
                        gtk_separator_new (GTK_ORIENTATION_VERTICAL));
        gtk_box_append (GTK_BOX (self->tool_options_box), gtk_label_new ("Size:"));
        GtkWidget *spin = gtk_spin_button_new_with_range (1, 200, 1);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (spin), radius);
        g_signal_connect (spin, "value-changed",
                          G_CALLBACK (on_radius_changed), self);
        gtk_box_append (GTK_BOX (self->tool_options_box), spin);
        self->radius_spin = spin;
    } else {
        gtk_box_append (GTK_BOX (self->tool_options_box),
                        gtk_separator_new (GTK_ORIENTATION_VERTICAL));
        GtkWidget *l = gtk_label_new ("(no adjustable options)");
        gtk_widget_add_css_class (l, "dim-label");
        gtk_box_append (GTK_BOX (self->tool_options_box), l);
        self->radius_spin = NULL;
    }
    update_status_bar (self);
}




static void
on_color_panel_rgba_changed (GtkColorDialogButton *b, GParamSpec *pp, gpointer ud)
{
    (void)pp;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (self->updating_ui) return;
    const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba (b);
    if (!rgba) return;
    Pixel px = {
        .r = (uint8_t)(rgba->red   * 255 + 0.5),
        .g = (uint8_t)(rgba->green * 255 + 0.5),
        .b = (uint8_t)(rgba->blue  * 255 + 0.5),
        .a = (uint8_t)(rgba->alpha * 255 + 0.5),
    };
    pencil_tool_set_color (self->pencil, px);
}


static void
on_swap_colors (GtkButton *b, gpointer ud)
{
    (void)b;
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->color_button) return;
    const GdkRGBA *cur = gtk_color_dialog_button_get_rgba (
        GTK_COLOR_DIALOG_BUTTON (self->color_button));
    GdkRGBA white = { 1, 1, 1, 1 };
    if (cur) set_pencil_color_from_rgba (self, cur);
    self->updating_ui = TRUE;
    gtk_color_dialog_button_set_rgba (
        GTK_COLOR_DIALOG_BUTTON (self->color_button), &white);
    self->updating_ui = FALSE;
}


static void
on_reset_colors (GtkButton *b, gpointer ud)
{
    (void)b;
    PaintWindow *self = PAINT_WINDOW (ud);
    GdkRGBA black = { 0, 0, 0, 1 };
    self->updating_ui = TRUE;
    gtk_color_dialog_button_set_rgba (
        GTK_COLOR_DIALOG_BUTTON (self->color_button), &black);
    self->updating_ui = FALSE;
    set_pencil_color_from_rgba (self, &black);
}


static void
on_swatch_clicked (GtkButton *b, gpointer ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    uint32_t c = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (b), "color"));
    GdkRGBA rgba = {
        ((c >> 16) & 0xFF) / 255.0,
        ((c >> 8)  & 0xFF) / 255.0,
        ( c        & 0xFF) / 255.0,
        1.0
    };
    self->updating_ui = TRUE;
    gtk_color_dialog_button_set_rgba (
        GTK_COLOR_DIALOG_BUTTON (self->color_button), &rgba);
    self->updating_ui = FALSE;
    set_pencil_color_from_rgba (self, &rgba);
}






/* Create the toggle buttons for tools (called before any panel builder). */
static void
create_tool_toggles (PaintWindow *self)
{
    self->tb_pencil     = make_tool_toggle (self, "pencil",     "\u270e");
    self->tb_eraser     = make_tool_toggle (self, "eraser",     "\u25a1");
    self->tb_eyedropper = make_tool_toggle (self, "eyedropper", "\u25c9");
    self->tb_blur       = make_tool_toggle (self, "blur",       "\u2248");
    self->tb_select     = make_tool_toggle (self, "select",     "\u25ad");

    gtk_widget_set_tooltip_text (self->tb_pencil,     "Pencil (Ctrl+P)");
    gtk_widget_set_tooltip_text (self->tb_eraser,     "Eraser (Ctrl+E)");
    gtk_widget_set_tooltip_text (self->tb_eyedropper, "Eyedropper (Ctrl+I)");
    gtk_widget_set_tooltip_text (self->tb_blur,       "Blur (Ctrl+B)");
    gtk_widget_set_tooltip_text (self->tb_select,     "Select (Ctrl+R)");
}



/* ═══════════════════════════════════════════════════════════════════
 * UI builders (docked-card layout)
 * ═══════════════════════════════════════════════════════════════════ */

/* Tool toggles: compact square buttons, 2-col grid, grouped by category. */

static GtkWidget *
build_action_toolbar (PaintWindow *self)
{
    (void)self;
    GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class (bar, "toolbar");

    struct { const char *icon; const char *tip; const char *action; } items[] = {
        { "document-new-symbolic",  "New (Ctrl+N)",            "win.new" },
        { "document-open-symbolic", "Open (Ctrl+O)",           "win.open" },
        { "document-save-symbolic", "Save (Ctrl+S)",           "win.save" },
        { NULL, NULL, NULL },
        { "edit-cut-symbolic",      "Cut (Ctrl+X)",            "win.cut" },
        { "edit-copy-symbolic",     "Copy (Ctrl+C)",           "win.copy" },
        { "edit-paste-symbolic",    "Paste (Ctrl+V)",          "win.paste" },
        { "edit-paste-symbolic",    "Paste as New Layer",      "win.paste-layer" },
        { NULL, NULL, NULL },
        { "edit-select-all-symbolic","Select All (Ctrl+A)",    "win.select-all" },
        { "edit-clear-symbolic",    "Deselect (Ctrl+D)",       "win.deselect" },
        { NULL, NULL, NULL },
        { "edit-undo-symbolic",     "Undo (Ctrl+Z)",           "win.undo" },
        { "edit-redo-symbolic",     "Redo (Ctrl+Shift+Z)",     "win.redo" },
    };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
        if (!items[i].icon) {
            gtk_box_append (GTK_BOX (bar),
                gtk_separator_new (GTK_ORIENTATION_VERTICAL));
            continue;
        }
        GtkWidget *b = gtk_button_new_from_icon_name (items[i].icon);
        gtk_widget_set_tooltip_text (b, items[i].tip);
        gtk_actionable_set_action_name (GTK_ACTIONABLE (b), items[i].action);
        gtk_box_append (GTK_BOX (bar), b);
    }
    return bar;
}

static GtkWidget *
build_tool_options_bar (PaintWindow *self)
{
    (void)self;
    GtkWidget *bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class (bar, "tool-options");
    return bar;
}

/* Card helper */
static GtkWidget *
make_card (const char *title, GtkWidget *body, GtkWidget *action_bar)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class (box, "panel-card");

    GtkWidget *hdr = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (hdr), title);
    gtk_widget_set_halign (hdr, GTK_ALIGN_START);
    gtk_widget_add_css_class (hdr, "panel-header");
    gtk_box_append (GTK_BOX (box), hdr);

    GtkWidget *wrap = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class (wrap, "panel-body");
    gtk_box_append (GTK_BOX (wrap), body);
    gtk_box_append (GTK_BOX (box), wrap);

    if (action_bar) {
        gtk_widget_add_css_class (action_bar, "card-action-bar");
        gtk_box_append (GTK_BOX (box), action_bar);
    }
    return box;
}

static GtkWidget *
build_tool_palette (PaintWindow *self)
{
    GtkWidget *grid = gtk_grid_new ();
    gtk_widget_add_css_class (grid, "tool-grid");
    gtk_grid_set_row_spacing    (GTK_GRID (grid), 3);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 3);

    int i = 0;
    struct { GtkWidget **btn; const char *tip; } tools[] = {
        { &self->tb_pencil,     "Pencil" },
        { &self->tb_eraser,     "Eraser" },
        { &self->tb_eyedropper, "Eyedropper" },
        { &self->tb_blur,       "Blur" },
        { &self->tb_select,     "Select rectangle" },
    };
    for (size_t k = 0; k < sizeof tools / sizeof tools[0]; k++) {
        if (*tools[k].btn) {
            gtk_grid_attach (GTK_GRID (grid), *tools[k].btn,
                             i % 2, i / 2, 1, 1);
            i++;
        }
    }
    /* Plugin tools */
    if (self->plugin_tools) {
        for (guint k = 0; k < self->plugin_tools->len; k++) {
            Tool *t = g_ptr_array_index (self->plugin_tools, k);
            if (!t || !t->vtable) continue;
            char *id = g_strdup_printf ("plugin:%u", k);
            GtkWidget *b = make_tool_toggle (self, id, "\u2728");
            gtk_widget_set_tooltip_text (b, t->vtable->name);
            gtk_grid_attach (GTK_GRID (grid), b, i % 2, i / 2, 1, 1);
            g_free (id);
            i++;
        }
    }
    return make_card ("<b>Tools</b>", grid, NULL);
}

/* Swatch grid — uses PaintSwatch, no CSS providers */
static void
on_swatch_gesture (GtkGestureClick *g, int n, double x, double y, gpointer ud)
{
    (void)g; (void)n; (void)x; (void)y;
    PaintWindow *self = PAINT_WINDOW (ud);
    GtkWidget *w = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (g));
    GdkRGBA c;
    paint_swatch_get_color (PAINT_SWATCH (w), &c);
    self->updating_ui = TRUE;
    gtk_color_dialog_button_set_rgba (
        GTK_COLOR_DIALOG_BUTTON (self->color_button), &c);
    self->updating_ui = FALSE;
    set_pencil_color_from_rgba (self, &c);
}

static GtkWidget *
build_color_panel (PaintWindow *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);

    /* Primary colour button + swap + reset */
    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    GtkColorDialog *cd = gtk_color_dialog_new ();
    self->color_button = gtk_color_dialog_button_new (cd);
    gtk_widget_set_size_request (self->color_button, 48, 48);
    const GdkRGBA black = { 0, 0, 0, 1 };
    gtk_color_dialog_button_set_rgba (
        GTK_COLOR_DIALOG_BUTTON (self->color_button), &black);
    g_signal_connect (self->color_button, "notify::rgba",
                      G_CALLBACK (on_color_panel_rgba_changed), self);
    gtk_box_append (GTK_BOX (row), self->color_button);

    GtkWidget *swap = gtk_button_new_with_label ("\u21c4");
    gtk_widget_set_tooltip_text (swap, "Swap / reset");
    g_signal_connect (swap, "clicked", G_CALLBACK (on_reset_colors), self);
    gtk_box_append (GTK_BOX (row), swap);
    gtk_box_append (GTK_BOX (box), row);

    /* Swatches — 6 columns × 4 rows */
    GtkWidget *grid = gtk_grid_new ();
    gtk_widget_add_css_class (grid, "swatch-grid");
    gtk_grid_set_row_spacing    (GTK_GRID (grid), 2);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 2);

    static const uint32_t palette[] = {
        0x000000, 0x404040, 0x808080, 0xC0C0C0, 0xFFFFFF, 0xFFF4E4,
        0x7F0000, 0xFF0000, 0xFF8000, 0xFFFF00, 0x80FF00, 0x00FF00,
        0x008000, 0x00FFFF, 0x0080FF, 0x0000FF, 0x8000FF, 0xFF00FF,
        0xFF80C0, 0x804000, 0xC08040, 0x4D7A97, 0x2E8B57, 0x8B4513,
    };
    for (int k = 0; k < 24; k++) {
        uint32_t c = palette[k];
        GdkRGBA rgba = {
            ((c >> 16) & 0xFF) / 255.0,
            ((c >> 8)  & 0xFF) / 255.0,
            ( c        & 0xFF) / 255.0,
            1.0
        };
        GtkWidget *sw = paint_swatch_new (&rgba);
        GtkGesture *g = gtk_gesture_click_new ();
        gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (g), 1);
        g_signal_connect (g, "pressed", G_CALLBACK (on_swatch_gesture), self);
        gtk_widget_add_controller (sw, GTK_EVENT_CONTROLLER (g));
        gtk_grid_attach (GTK_GRID (grid), sw, k % 6, k / 6, 1, 1);
    }
    gtk_box_append (GTK_BOX (box), grid);

    return make_card ("<b>Colors</b>", box, NULL);
}

static GtkWidget *
build_history_panel (PaintWindow *self)
{
    (void)self;
    GtkWidget *list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);

    GtkWidget *r = gtk_list_box_row_new ();
    GtkWidget *lbl = gtk_label_new ("Open Image");
    gtk_widget_set_halign (lbl, GTK_ALIGN_START);
    gtk_widget_set_margin_start (lbl, 6);
    gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (r), lbl);
    gtk_list_box_append (GTK_LIST_BOX (list), r);

    GtkWidget *action = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget *u = gtk_button_new_from_icon_name ("edit-undo-symbolic");
    GtkWidget *d = gtk_button_new_from_icon_name ("edit-redo-symbolic");
    gtk_widget_set_sensitive (u, FALSE);
    gtk_widget_set_sensitive (d, FALSE);
    gtk_widget_set_tooltip_text (u, "Undo (not implemented yet)");
    gtk_widget_set_tooltip_text (d, "Redo (not implemented yet)");
    gtk_box_append (GTK_BOX (action), u);
    gtk_box_append (GTK_BOX (action), d);

    return make_card ("<b>History</b>", list, action);
}

/* Blend mode dropdown handler */
static void
on_blend_mode_changed (GtkDropDown *dd, GParamSpec *p, gpointer ud)
{
    (void)p;
    PaintWindow *self = PAINT_WINDOW (ud);
    guint idx = gtk_drop_down_get_selected (dd);
    if (idx == GTK_INVALID_LIST_POSITION || !self->document) return;
    if (idx > BLEND_SCREEN) idx = BLEND_NORMAL;
    document_layer_set_blend_mode (self->document,
        document_active_layer (self->document), (BlendMode) idx);
}

static GtkWidget *
build_layer_panel (PaintWindow *self)
{
    /* List + blend/opacity controls + action bar */
    GtkWidget *outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);

    GtkWidget *scroll = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (scroll, TRUE);
    gtk_widget_set_size_request (scroll, -1, 140);

    self->layer_list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->layer_list),
                                     GTK_SELECTION_SINGLE);
    g_signal_connect (self->layer_list, "row-selected",
                      G_CALLBACK (on_row_selected), self);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll),
                                   self->layer_list);
    gtk_box_append (GTK_BOX (outer), scroll);

    /* Blend mode */
    GtkWidget *bm_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append (GTK_BOX (bm_row), gtk_label_new ("Blend:"));
    const char *bms[] = { "Normal", "Multiply", "Screen", NULL };
    GtkStringList *bm_model = gtk_string_list_new (bms);
    GtkWidget *bm_dd = gtk_drop_down_new (G_LIST_MODEL (bm_model), NULL);
    gtk_widget_set_hexpand (bm_dd, TRUE);
    g_signal_connect (bm_dd, "notify::selected",
                      G_CALLBACK (on_blend_mode_changed), self);
    gtk_box_append (GTK_BOX (bm_row), bm_dd);
    gtk_box_append (GTK_BOX (outer), bm_row);

    /* Opacity */
    GtkWidget *op_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append (GTK_BOX (op_row), gtk_label_new ("Alpha:"));
    self->opacity_scale = gtk_scale_new_with_range (
        GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value (GTK_SCALE (self->opacity_scale), FALSE);
    gtk_widget_set_hexpand (self->opacity_scale, TRUE);
    self->updating_ui = TRUE;
    gtk_range_set_value (GTK_RANGE (self->opacity_scale), 100);
    self->updating_ui = FALSE;
    g_signal_connect (self->opacity_scale, "value-changed",
                      G_CALLBACK (on_opacity_changed), self);
    gtk_box_append (GTK_BOX (op_row), self->opacity_scale);
    gtk_box_append (GTK_BOX (outer), op_row);

    /* Action bar with icons */
    GtkWidget *action = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
    struct { const char *icon; const char *tip; GCallback cb; } btns[] = {
        { "list-add-symbolic",       "Add Layer",       G_CALLBACK (on_layer_add_clicked) },
        { "list-remove-symbolic",    "Delete Layer",    G_CALLBACK (on_layer_delete_clicked) },
        { "go-up-symbolic",          "Move Up",         G_CALLBACK (on_layer_up_clicked) },
        { "go-down-symbolic",        "Move Down",       G_CALLBACK (on_layer_down_clicked) },
        { "object-flip-vertical-symbolic", "Merge Down", G_CALLBACK (on_layer_merge_clicked) },
    };
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++) {
        GtkWidget *b = gtk_button_new_from_icon_name (btns[i].icon);
        gtk_widget_set_tooltip_text (b, btns[i].tip);
        g_signal_connect (b, "clicked", btns[i].cb, self);
        gtk_box_append (GTK_BOX (action), b);
    }

    return make_card ("<b>Layers</b>", outer, action);
}

static void
paint_window_init (PaintWindow *self)
{
    gtk_window_set_default_size (GTK_WINDOW (self), 1300, 820);

    /* Tools */
    self->pencil     = pencil_tool_new ();
    self->eraser     = eraser_tool_new ();
    self->eyedropper = eyedropper_tool_new ();
    self->blur       = blur_tool_new ();
    self->select     = select_rect_tool_new ();
    eyedropper_tool_set_callback (self->eyedropper, on_eyedropper_picked, self);

    self->plugin_tools = g_ptr_array_new_with_free_func (g_free);
    for (int i = 0; i < plugin_registry_count (); i++) {
        Tool *t = plugin_registry_create (i);
        if (t) g_ptr_array_add (self->plugin_tools, t);
    }
    tool_engine_set_active (self->pencil);

    config_load_window_state (self);

    /* Actions */
    g_action_map_add_action_entries (G_ACTION_MAP (self),
                                     win_actions, G_N_ELEMENTS (win_actions), self);
    self->act_layer_delete = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "layer-delete"));
    self->act_layer_up     = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "layer-up"));
    self->act_layer_down   = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "layer-down"));
    self->act_layer_merge  = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "layer-merge-down"));
    self->act_cut          = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "cut"));
    self->act_copy         = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "copy"));
    self->act_paste        = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "paste"));
    self->act_paste_layer  = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "paste-layer"));
    self->act_select_all   = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "select-all"));
    self->act_deselect     = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "deselect"));
    self->act_delete_sel   = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self), "delete-sel"));

    /* Create toggles BEFORE any panel builder references them */
    create_tool_toggles (self);

    /* Canvas */
    self->canvas = paint_canvas_new ();
    gtk_widget_add_css_class (self->canvas, "paint-canvas");
    GtkWidget *scroll = gtk_scrolled_window_new ();
    gtk_widget_add_css_class (scroll, "canvas-scroll");
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), self->canvas);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand (scroll, TRUE);
    gtk_widget_set_vexpand (scroll, TRUE);
    self->hadj = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (scroll));
    self->vadj = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (scroll));
    paint_canvas_set_scroll_adjustments (PAINT_CANVAS (self->canvas),
                                          self->hadj, self->vadj);

    g_signal_connect (self->canvas, "cursor-moved",
                      G_CALLBACK (on_cursor_moved), self);
    g_signal_connect (self->canvas, "zoom-changed",
                      G_CALLBACK (on_zoom_changed), self);
    g_signal_connect (self->canvas, "pick-color",
                      G_CALLBACK (on_pick_color), self);

    /* Layout */
    GtkWidget *root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child (GTK_WINDOW (self), root);

    /* Menu row */
    GtkWidget *menu_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class (menu_row, "toolbar");
    GtkWidget *mb = build_menubar ();
    gtk_widget_set_hexpand (mb, TRUE);
    gtk_box_append (GTK_BOX (menu_row), mb);
    GtkWidget *b_prefs = gtk_button_new_from_icon_name ("emblem-system-symbolic");
    GtkWidget *b_about = gtk_button_new_from_icon_name ("help-about-symbolic");
    gtk_widget_set_tooltip_text (b_prefs, "Preferences (Ctrl+,)");
    gtk_widget_set_tooltip_text (b_about, "About Paint C");
    g_signal_connect (b_prefs, "clicked", G_CALLBACK (on_btn_prefs), self);
    g_signal_connect (b_about, "clicked", G_CALLBACK (on_btn_about), self);
    gtk_box_append (GTK_BOX (menu_row), b_prefs);
    gtk_box_append (GTK_BOX (menu_row), b_about);
    gtk_box_append (GTK_BOX (root), menu_row);

    /* Action toolbar */
    gtk_box_append (GTK_BOX (root), build_action_toolbar (self));

    /* Tool options */
    self->tool_options_box = build_tool_options_bar (self);
    gtk_box_append (GTK_BOX (root), self->tool_options_box);

    /* Main body: left dock | canvas | right dock */
    GtkWidget *body = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

    GtkWidget *left = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class (left, "dock");
    gtk_widget_set_size_request (left, 160, -1);
    self->panel_tools  = build_tool_palette (self);
    self->panel_colors = build_color_panel (self);
    gtk_box_append (GTK_BOX (left), self->panel_tools);
    gtk_box_append (GTK_BOX (left), self->panel_colors);
    gtk_box_append (GTK_BOX (body), left);

    gtk_box_append (GTK_BOX (body), scroll);

    GtkWidget *right = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class (right, "dock");
    gtk_widget_add_css_class (right, "dock-right");
    gtk_widget_set_size_request (right, 240, -1);
    self->panel_history = build_history_panel (self);
    self->panel_layers  = build_layer_panel (self);
    gtk_box_append (GTK_BOX (right), self->panel_history);
    gtk_box_append (GTK_BOX (right), self->panel_layers);
    gtk_box_append (GTK_BOX (body), right);

    gtk_widget_set_vexpand (body, TRUE);
    gtk_box_append (GTK_BOX (root), body);

    /* Status bar */
    GtkWidget *status = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class (status, "status-bar");
    self->st_hint   = gtk_label_new ("Pick a tool to begin.");
    self->st_dim    = gtk_label_new ("—");
    self->st_cursor = gtk_label_new ("0, 0");
    self->st_zoom   = gtk_label_new ("100%");
    gtk_widget_set_hexpand (self->st_hint, TRUE);
    gtk_label_set_xalign (GTK_LABEL (self->st_hint), 0.0f);
    gtk_box_append (GTK_BOX (status), self->st_hint);
    gtk_box_append (GTK_BOX (status), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
    gtk_box_append (GTK_BOX (status), self->st_dim);
    gtk_box_append (GTK_BOX (status), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
    gtk_box_append (GTK_BOX (status), self->st_cursor);
    gtk_box_append (GTK_BOX (status), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
    GtkWidget *z_out = gtk_button_new_with_label ("−");
    GtkWidget *z_in  = gtk_button_new_with_label ("+");
    GtkWidget *z_fit = gtk_button_new_from_icon_name ("zoom-fit-best-symbolic");
    gtk_widget_set_tooltip_text (z_out, "Zoom Out (Ctrl+-)");
    gtk_widget_set_tooltip_text (z_in,  "Zoom In (Ctrl+=)");
    gtk_widget_set_tooltip_text (z_fit, "Fit to window (Ctrl+0)");
    g_signal_connect (z_out, "clicked", G_CALLBACK (on_btn_zoom_out), self);
    g_signal_connect (z_in,  "clicked", G_CALLBACK (on_btn_zoom_in),  self);
    g_signal_connect (z_fit, "clicked", G_CALLBACK (on_btn_zoom_fit), self);
    gtk_box_append (GTK_BOX (status), z_out);
    gtk_box_append (GTK_BOX (status), self->st_zoom);
    gtk_box_append (GTK_BOX (status), z_in);
    gtk_box_append (GTK_BOX (status), z_fit);
    gtk_box_append (GTK_BOX (root), status);

    /* Accelerators */
    GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));
    if (app) {
        struct { const char *act; const char *accel; } map[] = {
            { "win.new",        "<Control>n" }, { "win.open",       "<Control>o" },
            { "win.save",       "<Control>s" }, { "win.save-as",    "<Control><Shift>s" },
            { "win.cut",        "<Control>x" }, { "win.copy",       "<Control>c" },
            { "win.paste",      "<Control>v" }, { "win.paste-layer","<Control><Shift>v" },
            { "win.select-all", "<Control>a" }, { "win.deselect",   "<Control>d" },
            { "win.delete-sel", "Delete"     },
            { "win.undo",       "<Control>z" }, { "win.redo",       "<Control><Shift>z" },
            { "win.tool-pencil",     "<Control>p" }, { "win.tool-eraser",     "<Control>e" },
            { "win.tool-eyedropper", "<Control>i" }, { "win.tool-blur",       "<Control>b" },
            { "win.tool-select",     "<Control>r" },
            { "win.zoom-in",  "<Control>equal" }, { "win.zoom-out", "<Control>minus" },
            { "win.zoom-fit", "<Control>0" },     { "win.zoom-100", "<Control>1" },
            { "win.preferences", "<Control>comma" },
        };
        for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
            const char *acc[] = { map[i].accel, NULL };
            gtk_application_set_accels_for_action (app, map[i].act, acc);
        }
    }

    /* Initial document */
    Document *doc = document_new (512, 512);
    Layer *bg = document_add_layer (doc, "Background");
    pixel_buffer_fill_rect (bg->buffer, 0, 0, 512, 512,
                            (Pixel){ .b = 255, .g = 255, .r = 255, .a = 255 });
    document_recomposite_all (doc);
    document_mark_clean (doc);

    self->document     = NULL;
    self->current_path = NULL;
    self->doc_obs      = NULL;
    self->updating_ui  = FALSE;

    paint_window_set_document (self, doc, NULL);
    sync_radius_spin (self);
    update_status_bar (self);
    update_cursor_radius (self);
    on_zoom_changed (PAINT_CANVAS (self->canvas), 1.0, self);
    tool_options_refresh (self);
}

static void
paint_window_finalize (GObject *obj)
{
    PaintWindow *self = PAINT_WINDOW (obj);
    config_save_window_state (self);
    if (self->document && self->doc_obs) {
        document_remove_observer (self->document, self->doc_obs);
        self->doc_obs = NULL;
    }
    if (self->document) document_free (self->document);
    g_free (self->current_path);
    /* TODO: proper tool destructors — currently leaked at process exit. */
    G_OBJECT_CLASS (paint_window_parent_class)->finalize (obj);
}

static void
paint_window_class_init (PaintWindowClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = paint_window_finalize;
}

GtkWidget *
paint_window_new (GtkApplication *app)
{
    return g_object_new (PAINT_TYPE_WINDOW, "application", app, NULL);
}
