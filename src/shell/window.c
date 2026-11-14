#include "window.h"
#include "../domain/document.h"
#include "../platform/canvas.h"
#include "../platform/file_io.h"
#include "../platform/navigator.h"
#include "../platform/clipboard.h"
#include "../interaction/tool_engine.h"
#include "../interaction/tools/pencil.h"
#include "../interaction/tools/eraser.h"
#include "../interaction/tools/eyedropper.h"
#include "../interaction/tools/blur.h"
#include "../interaction/tools/select_rect.h"
#include "../interaction/tools/line.h"
#include "../interaction/tools/move.h"
#include "../plugin/registry.h"
#include "../util/log.h"
#include "../util/config.h"
#include "swatch.h"
#include <string.h>

struct _PaintWindow {
    GtkApplicationWindow parent_instance;
    Document *document;
    char *current_path;
    Tool *pencil, *eraser, *eyedropper, *blur, *select, *line, *move;
    GPtrArray *plugin_tools;
    
    GtkWidget *canvas;
    GtkWidget *layer_list;
    GtkWidget *color_button;
    GtkWidget *radius_spin;
    GtkWidget *opacity_scale;
    GtkWidget *navigator;
    GtkAdjustment *hadj, *vadj;
    
    GtkWidget *tb_pencil, *tb_eraser, *tb_eyedropper, *tb_blur, *tb_select, *tb_line, *tb_move;
    
    GSimpleAction *act_layer_delete, *act_layer_up, *act_layer_down, *act_layer_merge;
    GSimpleAction *act_cut, *act_copy, *act_paste, *act_paste_layer;
    GSimpleAction *act_select_all, *act_deselect, *act_delete_sel;
    GSimpleAction *act_undo, *act_redo;
    
    DocumentObserverHandle doc_obs;
    gboolean updating_ui;
    
    GtkWidget *st_hint, *st_dim, *st_cursor, *st_zoom;
    GtkWidget *panel_tools, *panel_colors, *panel_history, *panel_layers;
    GtkWidget *history_list;
    GtkWidget *tool_options_box;
};

G_DEFINE_FINAL_TYPE (PaintWindow, paint_window, GTK_TYPE_APPLICATION_WINDOW)

/* ── Forward Declarations ───────────────────────────────────────────── */
static void update_title (PaintWindow *self);
static void update_status_bar (PaintWindow *self);
static void update_history_panel (PaintWindow *self);
static void tool_options_refresh (PaintWindow *self);
static void update_cursor_radius (PaintWindow *self);
static void set_pencil_color_from_rgba (PaintWindow *self, const GdkRGBA *rgba);
static GtkWidget *make_tool_toggle (PaintWindow *self, const char *id, const char *label);
static GtkWidget *build_tool_palette (PaintWindow *self);
static GtkWidget *build_color_panel (PaintWindow *self);
static GtkWidget *build_history_panel (PaintWindow *self);
static GtkWidget *build_layer_panel (PaintWindow *self);
static GtkWidget *build_action_toolbar (PaintWindow *self);
static GtkWidget *build_tool_options_bar (PaintWindow *self);
static GtkWidget *build_menubar (PaintWindow *self);
static void on_color_panel_rgba_changed (GtkColorDialogButton *b, GParamSpec *p, gpointer ud);
static void on_radius_changed (GtkSpinButton *spin, gpointer ud);
static void on_opacity_changed (GtkRange *range, gpointer ud);
static void on_tool_toggled (GtkToggleButton *btn, gpointer ud);
static void on_pick_color (PaintCanvas *canvas, double ix, double iy, gpointer ud);
static void on_doc_selection_changed (void *ud);
static void on_doc_history_changed (void *ud);
static void on_doc_composite_changed (void *ud);
static void on_doc_layer_list_changed (void *ud);
static void on_doc_active_layer_changed (void *ud);
static void action_layer_add (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_delete (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_up (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_down (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_merge (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_layer_duplicate (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_zoom_in (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_zoom_out (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_zoom_fit (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_zoom_100 (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_tool_pencil (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_tool_eraser (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_tool_eyedropper (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_tool_blur (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_tool_select (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_tool_line (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_tool_move (GSimpleAction *a, GVariant *p, gpointer ud);
static void action_preferences (GSimpleAction *a, GVariant *p, gpointer ud);
static void on_layer_add_clicked (GtkButton *b, gpointer ud);
static void on_layer_delete_clicked (GtkButton *b, gpointer ud);
static void on_layer_up_clicked (GtkButton *b, gpointer ud);
static void on_layer_down_clicked (GtkButton *b, gpointer ud);
static void on_layer_merge_clicked (GtkButton *b, gpointer ud);
static void on_row_selected (GtkListBox *list, GtkListBoxRow *row, gpointer ud);
static void on_visibility_toggled (GtkCheckButton *check, gpointer ud);
static void rebuild_layer_panel (PaintWindow *self);
static void sync_opacity_scale (PaintWindow *self);
static void update_layer_actions (PaintWindow *self);
static void on_eyedropper_picked (Pixel color, void *ud);
static void on_btn_prefs (GtkButton *b, gpointer ud);
static void on_btn_about (GtkButton *b, gpointer ud);
static void config_load_window_state (PaintWindow *self);
static void config_save_window_state (PaintWindow *self);
static void commit_active_tool (PaintWindow *self);
static void create_tool_toggles (PaintWindow *self);

/* ── Helpers ────────────────────────────────────────────────────────── */
static void update_title (PaintWindow *self) {
    if (!self->document) return;
    const char *raw = self->current_path ? self->current_path : "Untitled";
    char *base = g_path_get_basename(raw);
    char *title = document_is_dirty(self->document) ? g_strdup_printf("Paint C — *%s", base) : g_strdup_printf("Paint C — %s", base);
    gtk_window_set_title(GTK_WINDOW(self), title);
    g_free(title); g_free(base);
}

static void commit_active_tool (PaintWindow *self) {
    Tool *t = tool_engine_get_active();
    if (t && t->vtable->deactivate) t->vtable->deactivate(t, self->document);
}

static void config_load_window_state (PaintWindow *self) {
    int w = config_get_int("window/width", 1200), h = config_get_int("window/height", 800);
    gtk_window_set_default_size(GTK_WINDOW(self), w, h);
    pencil_tool_set_radius(self->pencil, config_get_int("tool/pencil/radius", 2));
    eraser_tool_set_radius(self->eraser, config_get_int("tool/eraser/radius", 8));
    blur_tool_set_radius(self->blur, config_get_int("tool/blur/radius", 6));
    Pixel c = { config_get_int("color/b", 0), config_get_int("color/g", 0), config_get_int("color/r", 0), 255 };
    pencil_tool_set_color(self->pencil, c);
}

static void config_save_window_state (PaintWindow *self) {
    if (!self) return;
    int w = gtk_widget_get_width(GTK_WIDGET(self)), h = gtk_widget_get_height(GTK_WIDGET(self));
    if (w > 0 && h > 0) { config_set_int("window/width", w); config_set_int("window/height", h); }
    if (self->pencil) config_set_int("tool/pencil/radius", pencil_tool_get_radius(self->pencil));
    if (self->eraser) config_set_int("tool/eraser/radius", eraser_tool_get_radius(self->eraser));
    if (self->blur) config_set_int("tool/blur/radius", blur_tool_get_radius(self->blur));
    if (self->color_button) {
        const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(self->color_button));
        if (rgba) {
            config_set_int("color/r", (int)(rgba->red*255+0.5));
            config_set_int("color/g", (int)(rgba->green*255+0.5));
            config_set_int("color/b", (int)(rgba->blue*255+0.5));
        }
    }
    config_save();
}

static gboolean on_close_request (GtkWindow *win, gpointer ud) { (void)win; config_save_window_state(PAINT_WINDOW(ud)); return FALSE; }

static void on_window_map (GtkWidget *widget, gpointer ud) {
    (void)widget; PaintWindow *self = PAINT_WINDOW(ud);
    if (self->canvas) gtk_widget_grab_focus(self->canvas);
}

/* ── Observers ──────────────────────────────────────────────────────── */
static void on_doc_composite_changed (void *ud) { PaintWindow *s = PAINT_WINDOW(ud); update_title(s); update_status_bar(s); }
static void on_doc_layer_list_changed (void *ud) { PaintWindow *s = PAINT_WINDOW(ud); rebuild_layer_panel(s); update_title(s); update_status_bar(s); }
static void on_doc_active_layer_changed (void *ud) {
    PaintWindow *s = PAINT_WINDOW(ud); if (!s->document) return;
    s->updating_ui = TRUE; int n = document_layer_count(s->document), a = document_active_layer(s->document);
    if (a >= 0) { GtkListBoxRow *r = gtk_list_box_get_row_at_index(GTK_LIST_BOX(s->layer_list), n - 1 - a); if (r) gtk_list_box_select_row(GTK_LIST_BOX(s->layer_list), r); }
    s->updating_ui = FALSE; update_layer_actions(s); sync_opacity_scale(s); update_status_bar(s);
}
static void on_doc_selection_changed (void *ud) { update_status_bar(PAINT_WINDOW(ud)); }
static void on_doc_history_changed (void *ud) { update_history_panel(PAINT_WINDOW(ud)); }

static const DocumentObserver win_observer = {
    .on_composite_changed = on_doc_composite_changed, .on_layer_list_changed = on_doc_layer_list_changed,
    .on_active_layer_changed = on_doc_active_layer_changed, .on_selection_changed = on_doc_selection_changed,
    .on_history_changed = on_doc_history_changed,
};

static void paint_window_set_document (PaintWindow *self, Document *doc, const char *path) {
    paint_canvas_set_document(PAINT_CANVAS(self->canvas), NULL);
    if (self->document && self->doc_obs) { document_remove_observer(self->document, self->doc_obs); self->doc_obs = NULL; }
    if (self->document) document_free(self->document);
    self->document = doc; g_free(self->current_path); self->current_path = path ? g_strdup(path) : NULL;
    paint_canvas_set_document(PAINT_CANVAS(self->canvas), doc);
    if (self->navigator) paint_navigator_set_document(PAINT_NAVIGATOR(self->navigator), doc);
    self->doc_obs = document_add_observer(doc, &win_observer, self);
    update_history_panel(self);
}

/* ── File Actions ───────────────────────────────────────────────────── */
static void show_error (PaintWindow *self, const char *title, const char *msg) {
    GtkAlertDialog *d = gtk_alert_dialog_new("%s", title); gtk_alert_dialog_set_detail(d, msg); gtk_alert_dialog_show(d, GTK_WINDOW(self));
}
static void on_new_ok_clicked (GtkButton *btn, gpointer ud) {
    (void)btn; GtkWidget *dialog = GTK_WIDGET(ud);
    int w = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_object_get_data(G_OBJECT(dialog), "width-spin")));
    int h = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(g_object_get_data(G_OBJECT(dialog), "height-spin")));
    PaintWindow *self = g_object_get_data(G_OBJECT(dialog), "self");
    Document *doc = document_new(w, h); Layer *bg = document_add_layer(doc, "Background");
    pixel_buffer_fill_rect(bg->buffer, 0, 0, w, h, (Pixel){.b=255, .g=255, .r=255, .a=255});
    document_invalidate(doc); document_clear_history(doc); paint_window_set_document(self, doc, NULL);
    gtk_window_destroy(GTK_WINDOW(dialog));
}
static void do_new_dialog (PaintWindow *self) {
    GtkWidget *dialog = gtk_window_new(); gtk_window_set_title(GTK_WINDOW(dialog), "New Document");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(self)); gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 12); gtk_widget_set_margin_end(vbox, 12); gtk_widget_set_margin_top(vbox, 12); gtk_widget_set_margin_bottom(vbox, 12);
    GtkWidget *grid = gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(grid), 6); gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    GtkWidget *sw = gtk_spin_button_new_with_range(16, 8192, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(sw), 512);
    GtkWidget *sh = gtk_spin_button_new_with_range(16, 8192, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(sh), 512);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Width:"), 0, 0, 1, 1); gtk_grid_attach(GTK_GRID(grid), sw, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Height:"), 0, 1, 1, 1); gtk_grid_attach(GTK_GRID(grid), sh, 1, 1, 1, 1);
    gtk_box_append(GTK_BOX(vbox), grid);
    GtkWidget *btnbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6); gtk_widget_set_halign(btnbox, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancel"); GtkWidget *ok = gtk_button_new_with_label("Create");
    gtk_box_append(GTK_BOX(btnbox), cancel); gtk_box_append(GTK_BOX(btnbox), ok); gtk_box_append(GTK_BOX(vbox), btnbox);
    g_object_set_data(G_OBJECT(dialog), "width-spin", sw); g_object_set_data(G_OBJECT(dialog), "height-spin", sh); g_object_set_data(G_OBJECT(dialog), "self", self);
    g_signal_connect_swapped(cancel, "clicked", G_CALLBACK(gtk_window_destroy), dialog); g_signal_connect(ok, "clicked", G_CALLBACK(on_new_ok_clicked), dialog);
    gtk_window_set_child(GTK_WINDOW(dialog), vbox); gtk_window_present(GTK_WINDOW(dialog));
}
static void on_open_response (GtkNativeDialog *dialog, int response, gpointer ud) {
    if (response == GTK_RESPONSE_ACCEPT) {
        PaintWindow *self = PAINT_WINDOW(ud); GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog)); char *path = g_file_get_path(file);
        GError *err = NULL; Document *doc = file_io_open_png(path, &err);
        if (doc) paint_window_set_document(self, doc, path);
        else { show_error(self, "Open failed", err ? err->message : "?"); if (err) g_error_free(err); }
        g_free(path); g_object_unref(file);
    }
    g_object_unref(dialog);
}
static void do_open_dialog (PaintWindow *self) {
    GtkFileChooserNative *d = gtk_file_chooser_native_new("Open Image", GTK_WINDOW(self), GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
    GtkFileFilter *f = gtk_file_filter_new(); gtk_file_filter_set_name(f, "Images (*.png)"); gtk_file_filter_add_pattern(f, "*.png");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f); g_signal_connect(d, "response", G_CALLBACK(on_open_response), self); gtk_native_dialog_show(GTK_NATIVE_DIALOG(d));
}
static void do_save_to_path (PaintWindow *self, const char *path) {
    char *p = g_strdup(path); if (!g_str_has_suffix(p, ".png")) { char *n = g_strconcat(p, ".png", NULL); g_free(p); p = n; }
    GError *err = NULL;
    if (file_io_save_png(self->document, p, &err)) { g_free(self->current_path); self->current_path = g_strdup(p); document_mark_clean(self->document); update_title(self); }
    else { show_error(self, "Save failed", err ? err->message : "?"); if (err) g_error_free(err); }
    g_free(p);
}
static void on_save_response (GtkNativeDialog *dialog, int response, gpointer ud) {
    if (response == GTK_RESPONSE_ACCEPT) {
        PaintWindow *self = PAINT_WINDOW(ud); GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
        char *path = g_file_get_path(file); do_save_to_path(self, path); g_free(path); g_object_unref(file);
    }
    g_object_unref(dialog);
}
static void do_save_as_dialog (PaintWindow *self) {
    GtkFileChooserNative *d = gtk_file_chooser_native_new("Save As", GTK_WINDOW(self), GTK_FILE_CHOOSER_ACTION_SAVE, "_Save", "_Cancel");
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d), "untitled.png");
    GtkFileFilter *f = gtk_file_filter_new(); gtk_file_filter_set_name(f, "PNG image"); gtk_file_filter_add_pattern(f, "*.png");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(d), f); g_signal_connect(d, "response", G_CALLBACK(on_save_response), self); gtk_native_dialog_show(GTK_NATIVE_DIALOG(d));
}
static void action_new (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; do_new_dialog(PAINT_WINDOW(ud)); }
static void action_open (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; do_open_dialog(PAINT_WINDOW(ud)); }
static void action_save (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->current_path) do_save_to_path(s, s->current_path); else do_save_as_dialog(s); }
static void action_save_as (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; do_save_as_dialog(PAINT_WINDOW(ud)); }

/* ── Edit / Clipboard Actions ───────────────────────────────────────── */
static void action_copy (GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; PaintWindow *self = PAINT_WINDOW(ud);
    if (!self->document) return;
    PixelBuffer *buf = document_copy_region(self->document);
    if (buf) { clipboard_set(buf); clipboard_set_system(buf); pixel_buffer_free(buf); }
}
static void action_cut (GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; PaintWindow *self = PAINT_WINDOW(ud);
    if (!self->document) return;
    PixelBuffer *buf = document_copy_region(self->document);
    if (buf) { clipboard_set(buf); clipboard_set_system(buf); pixel_buffer_free(buf); document_clear_region(self->document); }
}
typedef struct { PaintWindow *win; } PasteCtx;
static void on_system_paste (PixelBuffer *buf, void *ud) {
    PasteCtx *ctx = ud;
    if (buf && ctx->win->document) {
        SelectionRect r = document_selection(ctx->win->document);
        int px = r.w > 0 ? r.x : 0; int py = r.h > 0 ? r.y : 0;
        document_paste_buffer(ctx->win->document, buf, px, py);
        document_set_selection(ctx->win->document, px, py, buf->width, buf->height);
    }
    g_free(ctx);
}
static void action_paste (GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; PaintWindow *self = PAINT_WINDOW(ud);
    if (!self->document) return;
    if (clipboard_has_internal()) {
        PixelBuffer *buf = clipboard_get_internal();
        if (buf) {
            SelectionRect r = document_selection(self->document);
            int px = r.w > 0 ? r.x : 0; int py = r.h > 0 ? r.y : 0;
            document_paste_buffer(self->document, buf, px, py);
            document_set_selection(self->document, px, py, buf->width, buf->height);
            pixel_buffer_free(buf); return;
        }
    }
    PasteCtx *ctx = g_new0(PasteCtx, 1); ctx->win = self; clipboard_get_system_async(on_system_paste, ctx);
}
static void action_undo (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->document) document_undo(s->document); }
static void action_redo (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->document) document_redo(s->document); }

static void action_select_all (GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; PaintWindow *self = PAINT_WINDOW(ud); if (!self->document) return;
    commit_active_tool(self);
    document_set_selection(self->document, 0, 0, document_width(self->document), document_height(self->document));
}
static void action_deselect (GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; PaintWindow *self = PAINT_WINDOW(ud); if (!self->document) return;
    commit_active_tool(self);
    document_clear_selection(self->document);
}
static void action_delete_sel (GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; PaintWindow *self = PAINT_WINDOW(ud); if (!self->document) return;
    commit_active_tool(self);
    document_clear_region(self->document);
}

/* ── Layer Actions (with commit) ────────────────────────────────────── */
static void action_layer_add (GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); commit_active_tool(s); char *n = g_strdup_printf("Layer %d", document_layer_count(s->document) + 1);
    document_add_layer(s->document, n); g_free(n);
}
static void action_layer_delete (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); commit_active_tool(s); document_remove_layer(s->document, document_active_layer(s->document)); }
static void action_layer_up (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); commit_active_tool(s); int i = document_active_layer(s->document); document_move_layer(s->document, i, i + 1); }
static void action_layer_down (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); commit_active_tool(s); int i = document_active_layer(s->document); document_move_layer(s->document, i, i - 1); }
static void action_layer_merge (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); commit_active_tool(s); document_merge_down(s->document, document_active_layer(s->document)); }
static void action_layer_duplicate (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); commit_active_tool(s); document_duplicate_layer(s->document, document_active_layer(s->document)); }

/* ── Toggles & Preferences ──────────────────────────────────────────── */
static void action_toggle_tools (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->panel_tools) gtk_widget_set_visible(s->panel_tools, !gtk_widget_get_visible(s->panel_tools)); }
static void action_toggle_colors (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->panel_colors) gtk_widget_set_visible(s->panel_colors, !gtk_widget_get_visible(s->panel_colors)); }
static void action_toggle_history (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->panel_history) gtk_widget_set_visible(s->panel_history, !gtk_widget_get_visible(s->panel_history)); }
static void action_toggle_layers (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->panel_layers) gtk_widget_set_visible(s->panel_layers, !gtk_widget_get_visible(s->panel_layers)); }
static void action_about (GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; PaintWindow *self = PAINT_WINDOW(ud); GtkWidget *d = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(d), "Paint C"); gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(d), "0.1.0");
    gtk_window_set_transient_for(GTK_WINDOW(d), GTK_WINDOW(self)); gtk_window_present(GTK_WINDOW(d));
}
static void action_preferences (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; (void)ud; LOG_INFO("Prefs"); }
static void action_zoom_in (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->canvas) paint_canvas_zoom_in(PAINT_CANVAS(s->canvas)); }
static void action_zoom_out (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->canvas) paint_canvas_zoom_out(PAINT_CANVAS(s->canvas)); }
static void action_zoom_fit (GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud);
    if (!s->hadj || !s->vadj || !s->canvas) return;
    paint_canvas_zoom_fit(PAINT_CANVAS(s->canvas), (int)gtk_adjustment_get_page_size(s->hadj), (int)gtk_adjustment_get_page_size(s->vadj));
}
static void action_zoom_100 (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->canvas) paint_canvas_set_zoom(PAINT_CANVAS(s->canvas), 1.0); }

/* ── Tool Actions ───────────────────────────────────────────────────── */
static void action_tool_pencil (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->tb_pencil) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->tb_pencil), TRUE); }
static void action_tool_eraser (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->tb_eraser) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->tb_eraser), TRUE); }
static void action_tool_eyedropper (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->tb_eyedropper) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->tb_eyedropper), TRUE); }
static void action_tool_blur (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->tb_blur) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->tb_blur), TRUE); }
static void action_tool_select (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->tb_select) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->tb_select), TRUE); }
static void action_tool_line (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->tb_line) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->tb_line), TRUE); }
static void action_tool_move (GSimpleAction *a, GVariant *p, gpointer ud) { (void)a; (void)p; PaintWindow *s = PAINT_WINDOW(ud); if (s->tb_move) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->tb_move), TRUE); }

static const GActionEntry win_actions[] = {
    { "new", action_new, NULL, NULL, NULL, {0} }, { "open", action_open, NULL, NULL, NULL, {0} },
    { "save", action_save, NULL, NULL, NULL, {0} }, { "save-as", action_save_as, NULL, NULL, NULL, {0} },
    { "copy", action_copy, NULL, NULL, NULL, {0} }, { "cut", action_cut, NULL, NULL, NULL, {0} }, { "paste", action_paste, NULL, NULL, NULL, {0} },
    { "undo", action_undo, NULL, NULL, NULL, {0} }, { "redo", action_redo, NULL, NULL, NULL, {0} },
    { "select-all", action_select_all, NULL, NULL, NULL, {0} }, { "deselect", action_deselect, NULL, NULL, NULL, {0} }, { "delete-sel", action_delete_sel, NULL, NULL, NULL, {0} },
    { "layer-add", action_layer_add, NULL, NULL, NULL, {0} }, { "layer-delete", action_layer_delete, NULL, NULL, NULL, {0} },
    { "layer-up", action_layer_up, NULL, NULL, NULL, {0} }, { "layer-down", action_layer_down, NULL, NULL, NULL, {0} },
    { "layer-merge-down", action_layer_merge, NULL, NULL, NULL, {0} }, { "layer-duplicate", action_layer_duplicate, NULL, NULL, NULL, {0} },
    { "tool-pencil", action_tool_pencil, NULL, NULL, NULL, {0} }, { "tool-eraser", action_tool_eraser, NULL, NULL, NULL, {0} },
    { "tool-eyedropper", action_tool_eyedropper, NULL, NULL, NULL, {0} }, { "tool-blur", action_tool_blur, NULL, NULL, NULL, {0} },
    { "tool-select", action_tool_select, NULL, NULL, NULL, {0} }, { "tool-line", action_tool_line, NULL, NULL, NULL, {0} },
    { "tool-move", action_tool_move, NULL, NULL, NULL, {0} },
    { "zoom-in", action_zoom_in, NULL, NULL, NULL, {0} }, { "zoom-out", action_zoom_out, NULL, NULL, NULL, {0} },
    { "zoom-fit", action_zoom_fit, NULL, NULL, NULL, {0} }, { "zoom-100", action_zoom_100, NULL, NULL, NULL, {0} },
    { "toggle-tools", action_toggle_tools, NULL, NULL, NULL, {0} }, { "toggle-colors", action_toggle_colors, NULL, NULL, NULL, {0} },
    { "toggle-history", action_toggle_history, NULL, NULL, NULL, {0} }, { "toggle-layers", action_toggle_layers, NULL, NULL, NULL, {0} },
    { "preferences", action_preferences, NULL, NULL, NULL, {0} }, { "about", action_about, NULL, NULL, NULL, {0} },
};

/* ── UI Callbacks ───────────────────────────────────────────────────── */
static void on_cursor_moved (PaintCanvas *canvas, double x, double y, gpointer ud) {
    (void)canvas; PaintWindow *self = PAINT_WINDOW(ud);
    if (self->st_cursor) { char buf[64]; snprintf(buf, sizeof buf, "%.0f, %.0f", x, y); gtk_label_set_text(GTK_LABEL(self->st_cursor), buf); }
}
static void on_zoom_changed (PaintCanvas *canvas, double zoom, gpointer ud) {
    (void)canvas; PaintWindow *self = PAINT_WINDOW(ud);
    if (self->st_zoom) { char buf[32]; snprintf(buf, sizeof buf, "%.0f%%", zoom * 100.0); gtk_label_set_text(GTK_LABEL(self->st_zoom), buf); }
    if (self->navigator && self->hadj && self->vadj) paint_navigator_set_scroll(PAINT_NAVIGATOR(self->navigator), self->hadj, self->vadj, zoom);
}
static void on_pick_color (PaintCanvas *canvas, double ix, double iy, gpointer ud) {
    (void)canvas; PaintWindow *self = PAINT_WINDOW(ud);
    if (!self->document || !self->color_button) return;
    int x = (int)ix, y = (int)iy, w = document_width(self->document), h = document_height(self->document);
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    const uint8_t *comp = document_composite(self->document); const Pixel *p = (const Pixel *)(comp + ((gsize)y * w + x) * 4);
    GdkRGBA rgba = { p->r/255.0, p->g/255.0, p->b/255.0, p->a/255.0 };
    self->updating_ui = TRUE; gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(self->color_button), &rgba); self->updating_ui = FALSE;
    set_pencil_color_from_rgba(self, &rgba);
}
static void set_pencil_color_from_rgba (PaintWindow *self, const GdkRGBA *rgba) {
    Pixel p = { (uint8_t)(rgba->blue*255+0.5), (uint8_t)(rgba->green*255+0.5), (uint8_t)(rgba->red*255+0.5), (uint8_t)(rgba->alpha*255+0.5) };
    pencil_tool_set_color(self->pencil, p);
}
static void on_color_panel_rgba_changed (GtkColorDialogButton *b, GParamSpec *p, gpointer ud) {
    (void)p; PaintWindow *self = PAINT_WINDOW(ud); if (self->updating_ui) return;
    const GdkRGBA *rgba = gtk_color_dialog_button_get_rgba(b); if (rgba) set_pencil_color_from_rgba(self, rgba);
}
static void on_eyedropper_picked (Pixel color, void *ud) {
    PaintWindow *self = PAINT_WINDOW(ud);
    GdkRGBA rgba = { color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0 };
    self->updating_ui = TRUE; gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(self->color_button), &rgba); self->updating_ui = FALSE;
    set_pencil_color_from_rgba(self, &rgba); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->tb_pencil), TRUE);
}
static void on_radius_changed (GtkSpinButton *spin, gpointer ud) {
    PaintWindow *self = PAINT_WINDOW(ud); if (self->updating_ui) return;
    int r = gtk_spin_button_get_value_as_int(spin); Tool *t = tool_engine_get_active();
    if (t == self->pencil) pencil_tool_set_radius(t, r); else if (t == self->eraser) eraser_tool_set_radius(t, r); else if (t == self->blur) blur_tool_set_radius(t, r);
    update_cursor_radius(self);
}
static void on_opacity_changed (GtkRange *range, gpointer ud) {
    PaintWindow *self = PAINT_WINDOW(ud); if (self->updating_ui || !self->document) return;
    document_layer_set_opacity(self->document, document_active_layer(self->document), (float)(gtk_range_get_value(range)/100.0));
}
static void on_tool_toggled (GtkToggleButton *btn, gpointer ud) {
    PaintWindow *self = PAINT_WINDOW(ud); if (self->updating_ui) return; if (!gtk_toggle_button_get_active(btn)) return;
    const char *id = g_object_get_data(G_OBJECT(btn), "tool-id");
    self->updating_ui = TRUE; Tool *picked = self->pencil;
    if (g_strcmp0(id, "pencil") == 0) picked = self->pencil; else if (g_strcmp0(id, "eraser") == 0) picked = self->eraser;
    else if (g_strcmp0(id, "eyedropper") == 0) picked = self->eyedropper; else if (g_strcmp0(id, "blur") == 0) picked = self->blur;
    else if (g_strcmp0(id, "select") == 0) picked = self->select; else if (g_strcmp0(id, "line") == 0) picked = self->line;
    else if (g_strcmp0(id, "move") == 0) picked = self->move;
    else { int idx = 0; if (sscanf(id, "plugin:%d", &idx) == 1 && self->plugin_tools && (guint)idx < self->plugin_tools->len) picked = g_ptr_array_index(self->plugin_tools, idx); }
    GtkWidget *all[] = { self->tb_pencil, self->tb_eraser, self->tb_eyedropper, self->tb_blur, self->tb_select, self->tb_line, self->tb_move };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) if (all[i] && all[i] != GTK_WIDGET(btn)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(all[i]), FALSE);
    tool_engine_set_active(picked, self->document); self->updating_ui = FALSE;
    tool_options_refresh(self); update_status_bar(self); update_cursor_radius(self);
}
static void update_cursor_radius (PaintWindow *self) {
    if (!self->canvas) return;
    Tool *t = tool_engine_get_active(); double r = 0;
    if (t == self->pencil) r = pencil_tool_get_radius(t); else if (t == self->eraser) r = eraser_tool_get_radius(t); else if (t == self->blur) r = blur_tool_get_radius(t);
    paint_canvas_set_cursor_radius(PAINT_CANVAS(self->canvas), r);
}
static void update_status_bar (PaintWindow *self) {
    if (!self->document) return;
    char buf[128];
    snprintf(buf, sizeof buf, "%d × %d", document_width(self->document), document_height(self->document));
    if (self->st_dim) gtk_label_set_text(GTK_LABEL(self->st_dim), buf);
    Tool *t = tool_engine_get_active();
    if (self->st_hint) gtk_label_set_text(GTK_LABEL(self->st_hint), t && t->vtable ? t->vtable->name : "Ready");
}
static GtkWidget *make_tool_toggle (PaintWindow *self, const char *id, const char *label) {
    GtkWidget *b = gtk_toggle_button_new(); gtk_button_set_child(GTK_BUTTON(b), gtk_image_new_from_icon_name(label)); g_object_set_data_full(G_OBJECT(b), "tool-id", g_strdup(id), g_free);
    g_signal_connect(b, "toggled", G_CALLBACK(on_tool_toggled), self); return b;
}
static GtkWidget *make_card (const char *title, GtkWidget *body, GtkWidget *action_bar) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_add_css_class(box, "panel-card");
    GtkWidget *hdr = gtk_label_new(NULL); gtk_label_set_markup(GTK_LABEL(hdr), title); gtk_widget_set_halign(hdr, GTK_ALIGN_START);
    gtk_widget_add_css_class(hdr, "panel-header"); gtk_box_append(GTK_BOX(box), hdr);
    GtkWidget *wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_add_css_class(wrap, "panel-body");
    gtk_box_append(GTK_BOX(wrap), body); gtk_box_append(GTK_BOX(box), wrap);
    if (action_bar) { gtk_widget_add_css_class(action_bar, "card-action-bar"); gtk_box_append(GTK_BOX(box), action_bar); }
    return box;
}

/* ── Panels ─────────────────────────────────────────────────────────── */
static void update_history_panel (PaintWindow *self) {
    if (!self->history_list || !self->document) return;
    GtkWidget *child; while ((child = gtk_widget_get_first_child(self->history_list))) gtk_list_box_remove(GTK_LIST_BOX(self->history_list), child);
    int len = document_history_length(self->document), cur = document_history_current_index(self->document);
    for (int i = 0; i < len; i++) {
        GtkWidget *row = gtk_list_box_row_new(), *lbl = gtk_label_new(document_history_label_at(self->document, i));
        gtk_widget_set_halign(lbl, GTK_ALIGN_START); gtk_widget_set_margin_start(lbl, 6);
        if (i <= cur) gtk_widget_add_css_class(lbl, "accent"); else gtk_widget_set_opacity(lbl, 0.4);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl); gtk_list_box_append(GTK_LIST_BOX(self->history_list), row);
    }
    if (self->act_undo) g_simple_action_set_enabled(self->act_undo, cur >= 0);
    if (self->act_redo) g_simple_action_set_enabled(self->act_redo, cur < len - 1);
}
static GtkWidget *build_history_panel (PaintWindow *self) {
    GtkWidget *list = gtk_list_box_new(); gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE); self->history_list = list;
    GtkWidget *scroll = gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_widget_set_size_request(scroll, -1, 250); gtk_widget_set_vexpand(scroll, FALSE); gtk_widget_set_valign(scroll, GTK_ALIGN_START);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    GtkWidget *action = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    GtkWidget *u = gtk_button_new_from_icon_name("edit-undo-symbolic"), *d = gtk_button_new_from_icon_name("edit-redo-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(u), "win.undo"); gtk_actionable_set_action_name(GTK_ACTIONABLE(d), "win.redo");
    gtk_box_append(GTK_BOX(action), u); gtk_box_append(GTK_BOX(action), d);
    GtkWidget *card = make_card("<b>History</b>", scroll, action); gtk_widget_set_vexpand(card, FALSE); return card;
}
static GtkWidget *build_tool_palette (PaintWindow *self) {
    GtkWidget *grid = gtk_grid_new(); gtk_widget_add_css_class(grid, "tool-grid");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3); gtk_grid_set_column_spacing(GTK_GRID(grid), 3);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER); gtk_widget_set_valign(grid, GTK_ALIGN_START);
    int i = 0; struct { GtkWidget **btn; } tools[] = { {&self->tb_pencil}, {&self->tb_eraser}, {&self->tb_eyedropper}, {&self->tb_blur}, {&self->tb_select}, {&self->tb_line}, {&self->tb_move} };
    for (size_t k = 0; k < sizeof tools / sizeof tools[0]; k++) if (*tools[k].btn) { gtk_grid_attach(GTK_GRID(grid), *tools[k].btn, i % 2, i / 2, 1, 1); i++; }
    if (self->plugin_tools) for (guint k = 0; k < self->plugin_tools->len; k++) {
        Tool *t = g_ptr_array_index(self->plugin_tools, k); if (!t || !t->vtable) continue;
        char *id = g_strdup_printf("plugin:%u", k); GtkWidget *b = make_tool_toggle(self, id, "application-x-addon-symbolic");
        gtk_widget_set_tooltip_text(b, t->vtable->name); gtk_grid_attach(GTK_GRID(grid), b, i % 2, i / 2, 1, 1); g_free(id); i++;
    }
    return make_card("<b>Tools</b>", grid, NULL);
}
static void on_swatch_gesture (GtkGestureClick *g, int n, double x, double y, gpointer ud) {
    (void)g; (void)n; (void)x; (void)y; PaintWindow *self = PAINT_WINDOW(ud);
    GtkWidget *w = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(g)); GdkRGBA c;
    paint_swatch_get_color(PAINT_SWATCH(w), &c); self->updating_ui = TRUE;
    gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(self->color_button), &c); self->updating_ui = FALSE;
    set_pencil_color_from_rgba(self, &c);
}
static GtkWidget *build_color_panel (PaintWindow *self) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6); GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkColorDialog *cd = gtk_color_dialog_new(); self->color_button = gtk_color_dialog_button_new(cd);
    gtk_widget_set_size_request(self->color_button, 48, 48);
    const GdkRGBA black = { 0, 0, 0, 1 }; gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(self->color_button), &black);
    g_signal_connect(self->color_button, "notify::rgba", G_CALLBACK(on_color_panel_rgba_changed), self);
    gtk_box_append(GTK_BOX(row), self->color_button); gtk_box_append(GTK_BOX(box), row);
    GtkWidget *grid = gtk_grid_new(); gtk_widget_add_css_class(grid, "swatch-grid");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2); gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER); gtk_widget_set_valign(grid, GTK_ALIGN_START);
    static const uint32_t palette[] = {
        0x000000, 0x404040, 0x808080, 0xC0C0C0, 0xFFFFFF, 0xFFF4E4, 0x7F0000, 0xFF0000, 0xFF8000, 0xFFFF00, 0x80FF00, 0x00FF00,
        0x008000, 0x00FFFF, 0x0080FF, 0x0000FF, 0x8000FF, 0xFF00FF, 0xFF80C0, 0x804000, 0xC08040, 0x4D7A97, 0x2E8B57, 0x8B4513
    };
    for (int k = 0; k < 24; k++) {
        uint32_t c = palette[k]; GdkRGBA rgba = { ((c>>16)&0xFF)/255.0, ((c>>8)&0xFF)/255.0, (c&0xFF)/255.0, 1.0 };
        GtkWidget *sw = paint_swatch_new(&rgba); GtkGesture *g = gtk_gesture_click_new(); gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), 1);
        g_signal_connect(g, "pressed", G_CALLBACK(on_swatch_gesture), self); gtk_widget_add_controller(sw, GTK_EVENT_CONTROLLER(g));
        gtk_grid_attach(GTK_GRID(grid), sw, k % 4, k / 4, 1, 1);
    }
    gtk_box_append(GTK_BOX(box), grid); return make_card("<b>Colors</b>", box, NULL);
}
static void on_row_selected (GtkListBox *list, GtkListBoxRow *row, gpointer ud) {
    (void)list; PaintWindow *self = PAINT_WINDOW(ud); if (self->updating_ui || !row || !self->document) return;
    int layer_idx = document_layer_count(self->document) - 1 - gtk_list_box_row_get_index(row); document_set_active_layer(self->document, layer_idx);
}
static void on_visibility_toggled (GtkCheckButton *check, gpointer ud) {
    PaintWindow *self = PAINT_WINDOW(ud); if (self->updating_ui || !self->document) return;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(check), "layer-index"));
    document_layer_set_visible(self->document, idx, gtk_check_button_get_active(check));
}
static void on_layer_add_clicked    (GtkButton *b, gpointer ud) { (void)b; action_layer_add   (NULL, NULL, ud); }
static void on_layer_delete_clicked (GtkButton *b, gpointer ud) { (void)b; action_layer_delete(NULL, NULL, ud); }
static void on_layer_up_clicked     (GtkButton *b, gpointer ud) { (void)b; action_layer_up    (NULL, NULL, ud); }
static void on_layer_down_clicked   (GtkButton *b, gpointer ud) { (void)b; action_layer_down  (NULL, NULL, ud); }
static void on_layer_merge_clicked  (GtkButton *b, gpointer ud) { (void)b; action_layer_merge (NULL, NULL, ud); }
static void on_btn_prefs (GtkButton *b, gpointer ud) { (void)b; action_preferences(NULL, NULL, ud); }
static void on_btn_about (GtkButton *b, gpointer ud) { (void)b; action_about(NULL, NULL, ud); }

static void rebuild_layer_panel (PaintWindow *self) {
    if (!self->layer_list || !self->document) return; self->updating_ui = TRUE;
    GtkWidget *child; while ((child = gtk_widget_get_first_child(self->layer_list))) gtk_list_box_remove(GTK_LIST_BOX(self->layer_list), child);
    int n = document_layer_count(self->document);
    for (int i = n - 1; i >= 0; i--) {
        Layer *l = document_layer_at(self->document, i); GtkWidget *row = gtk_list_box_row_new(); GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(box, 4); gtk_widget_set_margin_end(box, 4); gtk_widget_set_margin_top(box, 2); gtk_widget_set_margin_bottom(box, 2);
        GtkWidget *check = gtk_check_button_new(); gtk_check_button_set_active(GTK_CHECK_BUTTON(check), l->visible);
        g_object_set_data(G_OBJECT(check), "layer-index", GINT_TO_POINTER(i));
        g_signal_connect(check, "toggled", G_CALLBACK(on_visibility_toggled), self); gtk_box_append(GTK_BOX(box), check);
        GtkWidget *label = gtk_label_new(l->name); gtk_label_set_xalign(GTK_LABEL(label), 0.0f); gtk_widget_set_hexpand(label, TRUE);
        gtk_box_append(GTK_BOX(box), label); gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box); gtk_list_box_append(GTK_LIST_BOX(self->layer_list), row);
    }
    int a = document_active_layer(self->document);
    if (a >= 0) { GtkListBoxRow *r = gtk_list_box_get_row_at_index(GTK_LIST_BOX(self->layer_list), n - 1 - a); if (r) gtk_list_box_select_row(GTK_LIST_BOX(self->layer_list), r); }
    self->updating_ui = FALSE; update_layer_actions(self); sync_opacity_scale(self);
}
static void update_layer_actions (PaintWindow *self) {
    if (!self->document) return;
    int n = document_layer_count(self->document), a = document_active_layer(self->document);
    g_simple_action_set_enabled(self->act_layer_delete, n > 1); g_simple_action_set_enabled(self->act_layer_up, a >= 0 && a < n - 1);
    g_simple_action_set_enabled(self->act_layer_down, a > 0); g_simple_action_set_enabled(self->act_layer_merge, a > 0);
}
static void sync_opacity_scale (PaintWindow *self) {
    if (!self->opacity_scale || !self->document) return;
    int a = document_active_layer(self->document); Layer *l = document_layer_at(self->document, a);
    self->updating_ui = TRUE; gtk_range_set_value(GTK_RANGE(self->opacity_scale), l ? l->opacity * 100.0 : 100.0); gtk_widget_set_sensitive(self->opacity_scale, l != NULL); self->updating_ui = FALSE;
}
static void tool_options_refresh (PaintWindow *self) {
    if (!self->tool_options_box) return;
    GtkWidget *child; while ((child = gtk_widget_get_first_child(self->tool_options_box))) gtk_box_remove(GTK_BOX(self->tool_options_box), child);
    Tool *t = tool_engine_get_active(); const char *name = t ? t->vtable->name : "None";
    GtkWidget *lbl = gtk_label_new(NULL); char *markup = g_markup_printf_escaped("<b>%s</b>", name); gtk_label_set_markup(GTK_LABEL(lbl), markup); g_free(markup);
    gtk_box_append(GTK_BOX(self->tool_options_box), lbl);
    int r = 2; if (t == self->pencil) r = pencil_tool_get_radius(t); else if (t == self->eraser) r = eraser_tool_get_radius(t); else if (t == self->blur) r = blur_tool_get_radius(t);
    if (t == self->pencil || t == self->eraser || t == self->blur) {
        gtk_box_append(GTK_BOX(self->tool_options_box), gtk_separator_new(GTK_ORIENTATION_VERTICAL)); gtk_box_append(GTK_BOX(self->tool_options_box), gtk_label_new("Size:"));
        GtkWidget *spin = gtk_spin_button_new_with_range(1, 200, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), r);
        g_signal_connect(spin, "value-changed", G_CALLBACK(on_radius_changed), self); gtk_box_append(GTK_BOX(self->tool_options_box), spin); self->radius_spin = spin;
    } else { self->radius_spin = NULL; }
}
static GtkWidget *build_action_toolbar (PaintWindow *self) {
    (void)self; GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2); gtk_widget_add_css_class(bar, "toolbar");
    struct { const char *icon; const char *action; } items[] = {
        { "document-new-symbolic", "win.new" }, { "document-open-symbolic", "win.open" }, { "document-save-symbolic", "win.save" }, { NULL, NULL },
        { "edit-cut-symbolic", "win.cut" }, { "edit-copy-symbolic", "win.copy" }, { "edit-paste-symbolic", "win.paste" }, { NULL, NULL },
        { "edit-undo-symbolic", "win.undo" }, { "edit-redo-symbolic", "win.redo" }
    };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
        if (!items[i].icon) { gtk_box_append(GTK_BOX(bar), gtk_separator_new(GTK_ORIENTATION_VERTICAL)); continue; }
        GtkWidget *b = gtk_button_new_from_icon_name(items[i].icon); gtk_actionable_set_action_name(GTK_ACTIONABLE(b), items[i].action); gtk_box_append(GTK_BOX(bar), b);
    }
    return bar;
}
static GtkWidget *build_tool_options_bar (PaintWindow *self) {
    (void)self; GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); gtk_widget_add_css_class(bar, "tool-options"); return bar;
}
static GtkWidget *build_layer_panel (PaintWindow *self) {
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4); GtkWidget *scroll = gtk_scrolled_window_new(); gtk_widget_set_vexpand(scroll, TRUE);
    self->layer_list = gtk_list_box_new(); gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->layer_list), GTK_SELECTION_SINGLE);
    g_signal_connect(self->layer_list, "row-selected", G_CALLBACK(on_row_selected), self); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), self->layer_list); gtk_box_append(GTK_BOX(outer), scroll);
    GtkWidget *action = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    struct { const char *icon; GCallback cb; } btns[] = { { "list-add-symbolic", G_CALLBACK(on_layer_add_clicked) }, { "list-remove-symbolic", G_CALLBACK(on_layer_delete_clicked) }, { "go-up-symbolic", G_CALLBACK(on_layer_up_clicked) }, { "go-down-symbolic", G_CALLBACK(on_layer_down_clicked) }, { "object-flip-vertical-symbolic", G_CALLBACK(on_layer_merge_clicked) } };
    for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++) { GtkWidget *b = gtk_button_new_from_icon_name(btns[i].icon); g_signal_connect(b, "clicked", btns[i].cb, self); gtk_box_append(GTK_BOX(action), b); }
    return make_card("<b>Layers</b>", outer, action);
}
static GtkWidget *build_menubar (PaintWindow *self) {
    (void)self; GMenu *file = g_menu_new(); g_menu_append(file, "New…", "win.new"); g_menu_append(file, "Open…", "win.open"); g_menu_append(file, "Save", "win.save"); g_menu_append(file, "Save As…", "win.save-as"); g_menu_append(file, "Quit", "app.quit");
    GMenu *edit = g_menu_new(); g_menu_append(edit, "Undo", "win.undo"); g_menu_append(edit, "Redo", "win.redo"); g_menu_append(edit, "Cut", "win.cut"); g_menu_append(edit, "Copy", "win.copy"); g_menu_append(edit, "Paste", "win.paste"); g_menu_append(edit, "Select All", "win.select-all"); g_menu_append(edit, "Deselect", "win.deselect"); g_menu_append(edit, "Delete Selection", "win.delete-sel"); g_menu_append(edit, "Preferences…", "win.preferences");
    GMenu *tools = g_menu_new(); g_menu_append(tools, "Pencil", "win.tool-pencil"); g_menu_append(tools, "Eraser", "win.tool-eraser"); g_menu_append(tools, "Eyedropper", "win.tool-eyedropper"); g_menu_append(tools, "Blur", "win.tool-blur"); g_menu_append(tools, "Select Rectangle", "win.tool-select"); g_menu_append(tools, "Line", "win.tool-line"); g_menu_append(tools, "Transform", "win.tool-move");
    GMenu *layer = g_menu_new(); g_menu_append(layer, "New Layer", "win.layer-add"); g_menu_append(layer, "Duplicate Layer", "win.layer-duplicate"); g_menu_append(layer, "Delete Layer", "win.layer-delete"); g_menu_append(layer, "Move Up", "win.layer-up"); g_menu_append(layer, "Move Down", "win.layer-down"); g_menu_append(layer, "Merge Down", "win.layer-merge-down");
    GMenu *view = g_menu_new(); g_menu_append(view, "Zoom In", "win.zoom-in"); g_menu_append(view, "Zoom Out", "win.zoom-out"); g_menu_append(view, "Fit to Window", "win.zoom-fit"); g_menu_append(view, "Actual Size (100%)", "win.zoom-100");
    GMenu *panels = g_menu_new(); g_menu_append(panels, "Tools Panel", "win.toggle-tools"); g_menu_append(panels, "Colors Panel", "win.toggle-colors"); g_menu_append(panels, "History Panel", "win.toggle-history"); g_menu_append(panels, "Layers Panel", "win.toggle-layers"); g_menu_append_section(view, NULL, G_MENU_MODEL(panels));
    GMenu *help = g_menu_new(); g_menu_append(help, "About Paint C", "win.about");
    GMenu *bar = g_menu_new(); g_menu_append_submenu(bar, "File", G_MENU_MODEL(file)); g_menu_append_submenu(bar, "Edit", G_MENU_MODEL(edit)); g_menu_append_submenu(bar, "Tools", G_MENU_MODEL(tools)); g_menu_append_submenu(bar, "Layer", G_MENU_MODEL(layer)); g_menu_append_submenu(bar, "View", G_MENU_MODEL(view)); g_menu_append_submenu(bar, "Help", G_MENU_MODEL(help));
    GtkWidget *menubar = gtk_popover_menu_bar_new_from_model(G_MENU_MODEL(bar));
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4); gtk_widget_add_css_class(row, "toolbar");
    gtk_widget_set_hexpand(menubar, TRUE); gtk_box_append(GTK_BOX(row), menubar);
    GtkWidget *prefs = gtk_button_new_from_icon_name("emblem-system-symbolic"), *about = gtk_button_new_from_icon_name("help-about-symbolic");
    gtk_widget_set_tooltip_text(prefs, "Preferences (Ctrl+,)"); gtk_widget_set_tooltip_text(about, "About Paint C");
    g_signal_connect(prefs, "clicked", G_CALLBACK(on_btn_prefs), self); g_signal_connect(about, "clicked", G_CALLBACK(on_btn_about), self);
    gtk_box_append(GTK_BOX(row), prefs); gtk_box_append(GTK_BOX(row), about); return row;
}

static void create_tool_toggles (PaintWindow *self) {
    self->tb_pencil = make_tool_toggle(self, "pencil", "document-edit-symbolic"); self->tb_eraser = make_tool_toggle(self, "eraser", "edit-clear-symbolic");
    self->tb_eyedropper = make_tool_toggle(self, "eyedropper", "color-select-symbolic"); self->tb_blur = make_tool_toggle(self, "blur", "weather-fog-symbolic");
    self->tb_select = make_tool_toggle(self, "select", "edit-select-all-symbolic"); self->tb_line = make_tool_toggle(self, "line", "insert-object-symbolic");
    self->tb_move = make_tool_toggle(self, "move", "transform-scale-symbolic");
}

static void paint_window_init (PaintWindow *self) {
    gtk_window_set_default_size(GTK_WINDOW(self), 1300, 820);
    g_signal_connect(self, "close-request", G_CALLBACK(on_close_request), self);
    g_signal_connect(self, "map", G_CALLBACK(on_window_map), self);

    self->pencil = pencil_tool_new(); self->eraser = eraser_tool_new(); self->eyedropper = eyedropper_tool_new();
    self->blur = blur_tool_new(); self->select = select_rect_tool_new(); self->line = line_tool_new(); self->move = move_tool_new();
    eyedropper_tool_set_callback(self->eyedropper, on_eyedropper_picked, self);
    
    self->plugin_tools = g_ptr_array_new_with_free_func(g_free);
    for (int i = 0; i < plugin_registry_count(); i++) { Tool *t = plugin_registry_create(i); if (t) g_ptr_array_add(self->plugin_tools, t); }
    tool_engine_set_active(self->pencil, self->document); config_load_window_state(self);
    
    g_action_map_add_action_entries(G_ACTION_MAP(self), win_actions, G_N_ELEMENTS(win_actions), self);
    self->act_layer_delete = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(self), "layer-delete"));
    self->act_layer_up = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(self), "layer-up"));
    self->act_layer_down = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(self), "layer-down"));
    self->act_layer_merge = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(self), "layer-merge-down"));
    self->act_undo = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(self), "undo"));
    self->act_redo = G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(self), "redo"));

    create_tool_toggles(self);
    self->canvas = paint_canvas_new(); gtk_widget_add_css_class(self->canvas, "paint-canvas");
    gtk_widget_set_focusable(self->canvas, TRUE); // Ensure focusable
    
    GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(center_box, GTK_ALIGN_CENTER); gtk_widget_set_valign(center_box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(center_box, TRUE); gtk_widget_set_vexpand(center_box, TRUE);
    gtk_box_append(GTK_BOX(center_box), self->canvas);

    GtkWidget *scroll = gtk_scrolled_window_new(); gtk_widget_add_css_class(scroll, "canvas-scroll");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), center_box);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE); gtk_widget_set_vexpand(scroll, TRUE);
    self->hadj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scroll)); self->vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
    paint_canvas_set_scroll_adjustments(PAINT_CANVAS(self->canvas), self->hadj, self->vadj);

    g_signal_connect(self->canvas, "cursor-moved", G_CALLBACK(on_cursor_moved), self);
    g_signal_connect(self->canvas, "zoom-changed", G_CALLBACK(on_zoom_changed), self);
    g_signal_connect(self->canvas, "pick-color", G_CALLBACK(on_pick_color), self);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_window_set_child(GTK_WINDOW(self), root);
    gtk_box_append(GTK_BOX(root), build_menubar(self));
    gtk_box_append(GTK_BOX(root), build_action_toolbar(self));
    self->tool_options_box = build_tool_options_bar(self); gtk_box_append(GTK_BOX(root), self->tool_options_box);

    GtkWidget *paned_outer = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *paned_inner = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_add_css_class(left, "dock");
    self->panel_tools = build_tool_palette(self); self->panel_colors = build_color_panel(self);
    gtk_box_append(GTK_BOX(left), self->panel_tools); gtk_box_append(GTK_BOX(left), self->panel_colors);
    gtk_paned_set_start_child(GTK_PANED(paned_outer), left); gtk_paned_set_resize_start_child(GTK_PANED(paned_outer), FALSE); gtk_paned_set_shrink_start_child(GTK_PANED(paned_outer), FALSE);
    gtk_paned_set_position(GTK_PANED(paned_outer), 180); gtk_widget_set_vexpand(left, FALSE);

    gtk_paned_set_start_child(GTK_PANED(paned_inner), scroll); gtk_paned_set_position(GTK_PANED(paned_inner), 800); gtk_widget_set_hexpand(scroll, TRUE);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_add_css_class(right, "dock");
    gtk_widget_add_css_class(right, "dock-right");
    self->panel_history = build_history_panel(self); self->panel_layers = build_layer_panel(self);
    gtk_box_append(GTK_BOX(right), self->panel_history); gtk_box_append(GTK_BOX(right), self->panel_layers);
    gtk_paned_set_end_child(GTK_PANED(paned_inner), right); gtk_paned_set_resize_end_child(GTK_PANED(paned_inner), TRUE); gtk_paned_set_shrink_end_child(GTK_PANED(paned_inner), FALSE);
    gtk_paned_set_end_child(GTK_PANED(paned_outer), paned_inner);

    gtk_widget_set_vexpand(paned_outer, TRUE); gtk_box_append(GTK_BOX(root), paned_outer);

    GtkWidget *status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8); gtk_widget_add_css_class(status, "status-bar");
    self->st_hint = gtk_label_new("Pick a tool to begin."); self->st_dim = gtk_label_new("—"); self->st_cursor = gtk_label_new("0, 0"); self->st_zoom = gtk_label_new("100%");
    gtk_widget_set_hexpand(self->st_hint, TRUE); gtk_label_set_xalign(GTK_LABEL(self->st_hint), 0.0f);
    gtk_box_append(GTK_BOX(status), self->st_hint); gtk_box_append(GTK_BOX(status), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    gtk_box_append(GTK_BOX(status), self->st_dim); gtk_box_append(GTK_BOX(status), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    gtk_box_append(GTK_BOX(status), self->st_cursor); gtk_box_append(GTK_BOX(status), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
    
    GtkWidget *z_out = gtk_button_new_with_label("−"); GtkWidget *z_in = gtk_button_new_with_label("+"); GtkWidget *z_fit = gtk_button_new_from_icon_name("zoom-fit-best-symbolic");
    g_signal_connect_swapped(z_out, "clicked", G_CALLBACK(paint_canvas_zoom_out), self->canvas);
    g_signal_connect_swapped(z_in, "clicked", G_CALLBACK(paint_canvas_zoom_in), self->canvas);
    g_signal_connect_swapped(z_fit, "clicked", G_CALLBACK(paint_canvas_zoom_fit), self->canvas);
    
    gtk_box_append(GTK_BOX(status), z_out); gtk_box_append(GTK_BOX(status), self->st_zoom); gtk_box_append(GTK_BOX(status), z_in); gtk_box_append(GTK_BOX(status), z_fit);
    gtk_box_append(GTK_BOX(root), status);

    /* ── GtkShortcutController (The Bulletproof Shortcut System) ────── */
    GtkShortcutController *shortcut_ctrl = (GtkShortcutController *)gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(shortcut_ctrl), GTK_SHORTCUT_SCOPE_GLOBAL);
    
    const char *keys[][2] = {
        {"<Control>n", "win.new"}, {"<Control>o", "win.open"}, {"<Control>s", "win.save"},
        {"<Control>z", "win.undo"}, {"<Control><Shift>z", "win.redo"}, {"<Control>y", "win.redo"},
        {"<Control>x", "win.cut"}, {"<Control>c", "win.copy"}, {"<Control>v", "win.paste"},
        {"<Control>a", "win.select-all"}, {"<Control>d", "win.deselect"},
        {"Escape", "win.deselect"}, {"Delete", "win.delete-sel"},
        {"<Control>equal", "win.zoom-in"}, {"<Control>plus", "win.zoom-in"}, {"<Control>minus", "win.zoom-out"}, {"<Control>0", "win.zoom-fit"},
        {"p", "win.tool-pencil"}, {"e", "win.tool-eraser"}, {"m", "win.tool-move"},
        {"s", "win.tool-select"}, {"l", "win.tool-line"}, {"b", "win.tool-blur"}, {"i", "win.tool-eyedropper"}
    };
    
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        GtkShortcutTrigger *trigger = gtk_shortcut_trigger_parse_string(keys[i][0]);
        GtkShortcutAction *action = gtk_named_action_new(keys[i][1]);
        gtk_shortcut_controller_add_shortcut(shortcut_ctrl, gtk_shortcut_new(trigger, action));
    }
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(shortcut_ctrl));

    Document *doc = document_new(512, 512); Layer *bg = document_add_layer(doc, "Background");
    pixel_buffer_fill_rect(bg->buffer, 0, 0, 512, 512, (Pixel){.b=255, .g=255, .r=255, .a=255});
    document_invalidate(doc); document_clear_history(doc);
    self->document = NULL; self->current_path = NULL; self->doc_obs = NULL; self->updating_ui = FALSE;
    paint_window_set_document(self, doc, NULL);
    update_status_bar(self); update_cursor_radius(self); on_zoom_changed(PAINT_CANVAS(self->canvas), 1.0, self); tool_options_refresh(self);
}

static void paint_window_finalize (GObject *obj) {
    PaintWindow *self = PAINT_WINDOW(obj); if (self->document && self->doc_obs) document_remove_observer(self->document, self->doc_obs);
    if (self->document) document_free(self->document); g_free(self->current_path);
    G_OBJECT_CLASS(paint_window_parent_class)->finalize(obj);
}
static void paint_window_class_init (PaintWindowClass *klass) { G_OBJECT_CLASS(klass)->finalize = paint_window_finalize; }
GtkWidget *paint_window_new (GtkApplication *app) { return g_object_new(PAINT_TYPE_WINDOW, "application", app, NULL); }
