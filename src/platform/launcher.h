/* launcher.h — first-run wizard + update check entry points.
 *
 * LAYER: L1 (platform/shell glue). GTK allowed.
 *
 * On the very first start (no "general/first_run_done" in config) paint-c
 * shows a small dialog that lets the user pick which addons from the
 * modules/addons folder are enabled, and optionally checks GitHub for a
 * newer release of both the app and the addon collection.
 */

#pragma once

#include <gtk/gtk.h>

/* Show the first-run dialog if this is the first launch. No-op otherwise.
 * Call after plugin_registry_init() and after the main window exists so
 * dialogs can be transient to it. */
void pnc_launcher_first_run (GtkWindow *parent);

/* Manually trigger an update check ("Check for Updates…" menu item).
 * Shows a result dialog when the check completes. */
void pnc_launcher_check_updates (GtkWindow *parent);
