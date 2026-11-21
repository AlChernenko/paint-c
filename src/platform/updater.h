/* updater.h — GitHub release/update checks (optional libsoup backend).
 *
 * LAYER: L1 (platform). When paint-c is built without libsoup,
 * pnc_update_check_available() returns FALSE and the check completes
 * immediately with a NULL result instead of failing the build.
 */

#pragma once

#include <glib.h>

typedef void (*PncUpdateCallback) (const char *latest_tag_or_null,
                                   gpointer user_data);

/* Asynchronously query https://api.github.com/repos/<slug>/releases/latest
 * and invoke `cb` on the main thread with the newest tag ("v0.2.0") when it
 * differs from `current_ver`, or NULL when up-to-date / offline / no HTTP
 * backend. Never blocks the UI. */
void    pnc_update_check_async  (const char *repo_slug,
                                 const char *current_ver,
                                 PncUpdateCallback cb,
                                 gpointer user_data);

gboolean pnc_update_check_available (void);
