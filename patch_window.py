import pathlib, re, sys

p = pathlib.Path('src/shell/window.c')
if not p.exists():
    sys.exit("src/shell/window.c not found — run from project root")
s = p.read_text()
changes = 0

def sub(old, new, label):
    global s, changes
    if old not in s:
        print(f"  SKIP {label} (pattern not found)")
        return
    s = s.replace(old, new, 1)
    changes += 1
    print(f"  OK   {label}")

# 1. include log.h
sub('#include "../interaction/tools/eraser.h"',
    '#include "../interaction/tools/eraser.h"\n#include "../util/log.h"',
    'include log.h')

# 2. status-bar fields in struct
sub('    DocumentObserverHandle doc_obs;\n    gboolean       updating_ui;\n};',
    '''    DocumentObserverHandle doc_obs;
    gboolean       updating_ui;

    /* Status bar labels */
    GtkWidget *st_doc;
    GtkWidget *st_layer;
    GtkWidget *st_tool;
    GtkWidget *st_cursor;
};''',
    'status-bar fields')

# 3. fix broken swapped connections
old_conn = '''    g_signal_connect_swapped (b_add,  "clicked", G_CALLBACK (action_layer_add),    self);
    g_signal_connect_swapped (b_del,  "clicked", G_CALLBACK (action_layer_delete), self);
    g_signal_connect_swapped (b_up,   "clicked", G_CALLBACK (action_layer_up),     self);
    g_signal_connect_swapped (b_down, "clicked", G_CALLBACK (action_layer_down),   self);
    g_signal_connect_swapped (b_mrg,  "clicked", G_CALLBACK (action_layer_merge),  self);'''
new_conn = '''    g_signal_connect (b_add,  "clicked", G_CALLBACK (on_layer_add_clicked),    self);
    g_signal_connect (b_del,  "clicked", G_CALLBACK (on_layer_delete_clicked), self);
    g_signal_connect (b_up,   "clicked", G_CALLBACK (on_layer_up_clicked),     self);
    g_signal_connect (b_down, "clicked", G_CALLBACK (on_layer_down_clicked),   self);
    g_signal_connect (b_mrg,  "clicked", G_CALLBACK (on_layer_merge_clicked),  self);'''
sub(old_conn, new_conn, 'layer button connections')

# 4. wrapper functions before build_layer_panel
marker = 'static GtkWidget *\nbuild_layer_panel (PaintWindow *self)'
wrappers = '''/* Button wrappers — g_signal_connect_swapped with 3-arg action handlers is
 * undefined behaviour, so we route clicks through 2-arg wrappers instead. */
static void
on_layer_add_clicked    (GtkButton *b, gpointer ud) { (void)b; action_layer_add   (NULL, NULL, ud); }
static void
on_layer_delete_clicked (GtkButton *b, gpointer ud) { (void)b; action_layer_delete (NULL, NULL, ud); }
static void
on_layer_up_clicked     (GtkButton *b, gpointer ud) { (void)b; action_layer_up     (NULL, NULL, ud); }
static void
on_layer_down_clicked   (GtkButton *b, gpointer ud) { (void)b; action_layer_down   (NULL, NULL, ud); }
static void
on_layer_merge_clicked  (GtkButton *b, gpointer ud) { (void)b; action_layer_merge  (NULL, NULL, ud); }

'''
sub(marker, wrappers + marker, 'wrapper functions')

# 5. remove dead on_new_dialog_response
dead = re.search(
    r'static void\non_new_dialog_response \(GtkDialog \*d, int response, gpointer ud\)\n\{.*?\n\}\n\n',
    s, re.DOTALL)
if dead:
    s = s[:dead.start()] + s[dead.end():]
    changes += 1
    print("  OK   remove dead on_new_dialog_response")
else:
    print("  SKIP dead function (already removed)")

# 6. update_status_bar function — insert before paint_window_init
status_fn = '''
static void
update_status_bar (PaintWindow *self)
{
    if (!self->st_doc) return;

    char buf[256];

    if (self->document) {
        snprintf (buf, sizeof buf, "Doc: %d × %d",
                  document_width (self->document), document_height (self->document));
        gtk_label_set_text (GTK_LABEL (self->st_doc), buf);

        int n = document_layer_count (self->document);
        int a = document_active_layer (self->document);
        Layer *l = document_layer_at (self->document, a);
        snprintf (buf, sizeof buf, "Layer: %s (%d/%d)",
                  l ? l->name : "—", a + 1, n);
        gtk_label_set_text (GTK_LABEL (self->st_layer), buf);
    } else {
        gtk_label_set_text (GTK_LABEL (self->st_doc), "Doc: —");
        gtk_label_set_text (GTK_LABEL (self->st_layer), "Layer: —");
    }

    Tool *t = tool_engine_get_active ();
    if (t && t->vtable && t->vtable->name) {
        snprintf (buf, sizeof buf, "Tool: %s", t->vtable->name);
        gtk_label_set_text (GTK_LABEL (self->st_tool), buf);
    } else {
        gtk_label_set_text (GTK_LABEL (self->st_tool), "Tool: —");
    }
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

'''
sub('static void\npaint_window_init (PaintWindow *self)',
    status_fn + 'static void\npaint_window_init (PaintWindow *self)',
    'update_status_bar + cursor handler')

# 7. build the status bar after the paned
sub('''    gtk_widget_set_vexpand    (paned, TRUE);
    gtk_box_append (GTK_BOX (root), paned);

    Document *doc = document_new (512, 512);''',
    '''    gtk_widget_set_vexpand    (paned, TRUE);
    gtk_box_append (GTK_BOX (root), paned);

    /* ── Status bar ──────────────────────────────────────────────── */
    GtkWidget *status = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start (status, 8);
    gtk_widget_set_margin_end   (status, 8);
    gtk_widget_set_margin_top   (status, 3);
    gtk_widget_set_margin_bottom(status, 3);
    self->st_doc    = gtk_label_new ("Doc: —");
    self->st_layer  = gtk_label_new ("Layer: —");
    self->st_tool   = gtk_label_new ("Tool: —");
    self->st_cursor = gtk_label_new ("Pos: —");
    gtk_box_append (GTK_BOX (status), self->st_doc);
    gtk_box_append (GTK_BOX (status), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
    gtk_box_append (GTK_BOX (status), self->st_layer);
    gtk_box_append (GTK_BOX (status), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
    gtk_box_append (GTK_BOX (status), self->st_tool);
    gtk_box_append (GTK_BOX (status), gtk_separator_new (GTK_ORIENTATION_VERTICAL));
    gtk_box_append (GTK_BOX (status), self->st_cursor);
    gtk_box_append (GTK_BOX (root), status);

    /* Wire cursor tracking from the canvas */
    g_signal_connect (self->canvas, "cursor-moved",
                      G_CALLBACK (on_cursor_moved), self);

    Document *doc = document_new (512, 512);''',
    'status bar construction')

# 8. call update_status_bar at end of paint_window_init
sub('''    paint_window_set_document (self, doc, NULL);
    sync_radius_spin (self);
}''',
    '''    paint_window_set_document (self, doc, NULL);
    sync_radius_spin (self);
    update_status_bar (self);
}''',
    'initial status-bar fill')

# 9. status-bar refresh on layer/active change
sub('''static void
on_doc_active_layer_changed (void *ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->document) return;''',
    '''static void
on_doc_active_layer_changed (void *ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    if (!self->document) return;
    update_status_bar (self);''',
    'status on active-layer change')

sub('''static void
on_doc_layer_list_changed (void *ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    rebuild_layer_panel (self);
    update_title (self);
}''',
    '''static void
on_doc_layer_list_changed (void *ud)
{
    PaintWindow *self = PAINT_WINDOW (ud);
    rebuild_layer_panel (self);
    update_title (self);
    update_status_bar (self);
}''',
    'status on layer-list change')

# 10. status-bar refresh on document swap
sub('''    self->doc_obs = document_add_observer (doc, &win_observer, self);
}''',
    '''    self->doc_obs = document_add_observer (doc, &win_observer, self);
    LOG_INFO ("document set: %dx%d (%s)", document_width (doc),
              document_height (doc), path ? path : "untitled");
}''',
    'log on doc swap')

# 11. status-bar refresh on tool change
sub('''    if (g_strcmp0 (id, "pencil") == 0) {
        gtk_toggle_button_set_active (self->tb_eraser, FALSE);
        tool_engine_set_active (self->pencil);
    } else if (g_strcmp0 (id, "eraser") == 0) {
        gtk_toggle_button_set_active (self->tb_pencil, FALSE);
        tool_engine_set_active (self->eraser);
    }
    self->updating_ui = FALSE;
    sync_radius_spin (self);''',
    '''    if (g_strcmp0 (id, "pencil") == 0) {
        gtk_toggle_button_set_active (self->tb_eraser, FALSE);
        tool_engine_set_active (self->pencil);
    } else if (g_strcmp0 (id, "eraser") == 0) {
        gtk_toggle_button_set_active (self->tb_pencil, FALSE);
        tool_engine_set_active (self->eraser);
    }
    self->updating_ui = FALSE;
    sync_radius_spin (self);
    update_status_bar (self);
    LOG_DEBUG ("tool switched to %s", id);''',
    'status on tool switch')

p.write_text(s)
print(f"\n{changes} patches applied.")
if changes < 10:
    sys.exit("Not all patches applied — check output above")
