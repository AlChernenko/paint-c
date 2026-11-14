/* config.h — persistent settings backed by GKeyFile.
 * Default path: $XDG_CONFIG_HOME/paint-c/settings.ini
 * Override:     PAINT_CONFIG=/some/path/settings.ini
 */
#pragma once
#include <glib.h>
#include <stdbool.h>

void        config_init      (void);   /* once at startup */
const char *config_path      (void);

const char *config_get_string (const char *key, const char *def);
int         config_get_int    (const char *key, int def);
bool        config_get_bool   (const char *key, bool def);
void        config_set_string (const char *key, const char *val);
void        config_set_int    (const char *key, int val);
void        config_set_bool   (const char *key, bool val);
void        config_save       (void);
