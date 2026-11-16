#include <gtk/gtk.h>
#include "shell/window.h"
#include "util/log.h"
#include "util/config.h"
#include "plugin/registry.h"

static void
on_activate (GtkApplication *app, gpointer user_data)
{
    (void)user_data;
    GtkWidget *win = paint_window_new (app);
    gtk_window_present (GTK_WINDOW (win));
}

int
main (int argc, char **argv)
{
    paint_log_init ();
    config_init ();

    /* Look for addons next to the build dir or via PAINT_ADDONS_PATH. */
    const char *addons = g_getenv ("PAINT_ADDONS_PATH");
    if (!addons) addons = "./build/addons";
    plugin_registry_init (addons);

    /* Load base application stylesheet */
    {
        GtkCssProvider *css = gtk_css_provider_new ();
        GdkDisplay *disp = gdk_display_get_default ();
        if (disp) {
            char *exe = g_file_read_link ("/proc/self/exe", NULL);
            char *css_path = NULL;
            if (exe) {
                char *dir = g_path_get_dirname (exe);
                css_path = g_build_filename (dir, "..", "..", "src", "shell", "style.css", NULL);
                if (!g_file_test (css_path, G_FILE_TEST_EXISTS)) {
                    g_free (css_path);
                    css_path = g_build_filename (dir, "..", "src", "shell", "style.css", NULL);
                }
                g_free (dir);
                g_free (exe);
            }
            if (css_path && g_file_test (css_path, G_FILE_TEST_EXISTS)) {
                gtk_css_provider_load_from_path (css, css_path);
                gtk_style_context_add_provider_for_display (disp, GTK_STYLE_PROVIDER (css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
                g_free (css_path);
            } else {
                LOG_WARN ("style.css not found — using defaults");
            }
        }
        g_object_unref (css);
    }

    GtkApplication *app = gtk_application_new ("org.paintc.PaintC", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect (app, "activate", G_CALLBACK (on_activate), NULL);
    int status = g_application_run (G_APPLICATION (app), argc, argv);
    g_object_unref (app);

    return status;
}
