/* launcher.c — see launcher.h. */

#include "launcher.h"
#include "updater.h"
#include "../plugin/registry.h"
#include "../util/config.h"
#include "../util/log.h"

#include <string.h>

#define PAINT_VERSION   "0.1.0"
#define PAINT_REPO_SLUG "paint-c/paint-c"   /* TODO: real GitHub owner/repo */

/* ── update result dialogs ────────────────────────────────────────── */

static void
show_info_dialog (GtkWindow *parent, const char *title, const char *msg)
{
    GtkWidget *d = gtk_message_dialog_new (parent,
                                           GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                           GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                           "%s", msg);
    gtk_window_set_title (GTK_WINDOW (d), title);
    g_signal_connect_swapped (d, "response", G_CALLBACK (gtk_window_destroy), d);
    gtk_window_present (GTK_WINDOW (d));
}

typedef struct { GtkWindow *parent; } UpdCtx;

static void
on_update_result (const char *latest, gpointer ud)
{
    UpdCtx *ctx = ud;
    if (latest) {
        char *msg = g_strdup_printf (
            "A newer version of paint-c is available:\n%s\n\n"
            "Current: " PAINT_VERSION " — download it from the GitHub releases page.",
            latest);
        show_info_dialog (ctx->parent, "Update available", msg);
        g_free (msg);
    } else if (!pnc_update_check_available ()) {
        show_info_dialog (ctx->parent, "Check for Updates",
                          "Update checking is not available in this build "
                          "(compiled without libsoup).");
    } else {
        show_info_dialog (ctx->parent, "Check for Updates",
                          "You are running the latest version (" PAINT_VERSION ").");
    }
    g_free (ctx);
}

void
pnc_launcher_check_updates (GtkWindow *parent)
{
    UpdCtx *ctx = g_new0 (UpdCtx, 1);
    ctx->parent = parent;
    pnc_update_check_async (PAINT_REPO_SLUG, PAINT_VERSION, on_update_result, ctx);
}

/* ── first-run addon picker ───────────────────────────────────────── */

typedef struct { GtkWindow *dialog; gboolean do_update; } FirstRunCtx;

static void
on_first_run_close (GtkButton *btn, gpointer ud)
{
    (void) btn;
    FirstRunCtx *fr = ud;
    GtkWidget *list = GTK_WIDGET (g_object_get_data (G_OBJECT (fr->dialog), "addon-list"));

    /* Persist checkbox states back into the plugin registry / config. */
    int n = plugin_registry_count ();
    for (int i = 0; i < n; i++) {
        GtkWidget *row = gtk_list_box_get_row_at_index (GTK_LIST_BOX (list), i);
        if (!row) continue;
        GtkWidget *sw = gtk_widget_get_first_child (row);      /* GtkSwitch */
        if (GTK_IS_SWITCH (sw))
            plugin_registry_set_enabled (i, gtk_switch_get_active (GTK_SWITCH (sw)));
    }

    gboolean do_update = fr->do_update;
    GtkWindow *parent = GTK_WINDOW (g_object_get_data (G_OBJECT (fr->dialog), "parent"));
    gtk_window_destroy (fr->dialog);

    config_set_bool ("general/first_run_done", TRUE);
    config_save ();

    if (do_update)
        pnc_launcher_check_updates (parent);
    g_free (fr);
}

static gboolean
on_update_switch_notify (GtkWidget *sw, GParamSpec *p, gpointer ud)
{
    (void) p;
    ((FirstRunCtx *) ud)->do_update = gtk_switch_get_active (GTK_SWITCH (sw));
    return FALSE;
}

void
pnc_launcher_first_run (GtkWindow *parent)
{
    if (config_get_bool ("general/first_run_done", FALSE))
        return;                         /* not the first run */

    LOG_INFO ("first run — showing module launcher");

    GtkWidget *dialog = gtk_window_new ();
    gtk_window_set_title (GTK_WINDOW (dialog), "Welcome to paint-c — first run");
    gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
    gtk_window_set_default_size (GTK_WINDOW (dialog), 420, 460);

    FirstRunCtx *fr = g_new0 (FirstRunCtx, 1);
    fr->dialog = dialog;
    fr->do_update = TRUE;

    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start (vbox, 14); gtk_widget_set_margin_end (vbox, 14);
    gtk_widget_set_margin_top (vbox, 14);   gtk_widget_set_margin_bottom (vbox, 14);

    GtkWidget *head = gtk_label_new (NULL);
    gtk_label_set_markup (GTK_LABEL (head),
        "<b>Welcome!</b>\nChoose which modules (addons) to enable. "
        "You can change this later in Preferences.");
    gtk_label_set_wrap (GTK_LABEL (head), TRUE);
    gtk_label_set_xalign (GTK_LABEL (head), 0.0f);
    gtk_box_append (GTK_BOX (vbox), head);

    GtkWidget *scroll = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (scroll, TRUE);
    GtkWidget *list = gtk_list_box_new ();
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), list);
    g_object_set_data (G_OBJECT (dialog), "addon-list", list);

    int n = plugin_registry_count ();
    if (n == 0) {
        GtkWidget *empty = gtk_label_new ("No addons found in the modules folder.");
        gtk_widget_set_opacity (empty, 0.5);
        gtk_list_box_append (GTK_LIST_BOX (list), empty);
    }
    for (int i = 0; i < n; i++) {
        const PaintPluginDescriptor *desc = plugin_registry_get_desc (i);
        if (!desc) continue;
        GtkWidget *row = gtk_list_box_row_new ();
        GtkWidget *rb = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start (rb, 6); gtk_widget_set_margin_end (rb, 6);
        gtk_widget_set_margin_top (rb, 4);   gtk_widget_set_margin_bottom (rb, 4);

        GtkWidget *lbl = gtk_label_new (NULL);
        char *mk = g_markup_printf_escaped ("<b>%s</b>\n<small>%s · v%s</small>",
                                            desc->name, desc->id,
                                            desc->version ? desc->version : "?");
        gtk_label_set_markup (GTK_LABEL (lbl), mk);
        g_free (mk);
        gtk_label_set_xalign (GTK_LABEL (lbl), 0.0f);
        gtk_widget_set_hexpand (lbl, TRUE);
        gtk_box_append (GTK_BOX (rb), lbl);

        GtkWidget *sw = gtk_switch_new ();
        gtk_switch_set_active (GTK_SWITCH (sw), plugin_registry_get_enabled (i));
        gtk_widget_set_valign (sw, GTK_ALIGN_CENTER);
        gtk_box_append (GTK_BOX (rb), sw);

        gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (row), rb);
        gtk_list_box_append (GTK_LIST_BOX (list), row);
    }
    gtk_box_append (GTK_BOX (vbox), scroll);

    /* Update opt-in */
    GtkWidget *upd_row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *upd_lbl = gtk_label_new ("Check GitHub for updates after setup");
    gtk_widget_set_hexpand (upd_lbl, TRUE);
    gtk_label_set_xalign (GTK_LABEL (upd_lbl), 0.0f);
    GtkWidget *upd_sw = gtk_switch_new ();
    gtk_switch_set_active (GTK_SWITCH (upd_sw), TRUE);
    g_signal_connect (upd_sw, "notify::active",
                      G_CALLBACK (on_update_switch_notify), fr);
    gtk_box_append (GTK_BOX (upd_row), upd_lbl);
    gtk_box_append (GTK_BOX (upd_row), upd_sw);
    gtk_box_append (GTK_BOX (vbox), upd_row);

    GtkWidget *btnbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign (btnbox, GTK_ALIGN_END);
    GtkWidget *start = gtk_button_new_with_label ("Start painting");
    gtk_widget_add_css_class (start, "suggested-action");
    g_object_set_data (G_OBJECT (dialog), "parent", parent);
    g_signal_connect (start, "clicked", G_CALLBACK (on_first_run_close), fr);
    gtk_box_append (GTK_BOX (btnbox), start);
    gtk_box_append (GTK_BOX (vbox), btnbox);

    gtk_window_set_child (GTK_WINDOW (dialog), vbox);
    gtk_window_present (GTK_WINDOW (dialog));
}
