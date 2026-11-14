#include "config.h"
#include "log.h"
#include <string.h>
#include <stdlib.h>

static GKeyFile *g_kf = NULL;
static char     *g_path = NULL;

void
config_init (void)
{
    if (g_kf) return;
    g_kf = g_key_file_new ();

    const char *override = g_getenv ("PAINT_CONFIG");
    if (override) g_path = g_strdup (override);
    else {
        char *dir = g_build_filename (g_get_user_config_dir (), "paint-c", NULL);
        g_mkdir_with_parents (dir, 0700);
        g_path = g_build_filename (dir, "settings.ini", NULL);
        g_free (dir);
    }

    GError *err = NULL;
    if (!g_key_file_load_from_file (g_kf, g_path,
                                    G_KEY_FILE_KEEP_COMMENTS, &err)) {
        if (err && err->code != G_FILE_ERROR_NOENT)
            LOG_WARN ("config load: %s", err->message);
        if (err) g_error_free (err);
    } else {
        LOG_INFO ("config loaded: %s", g_path);
    }
}

const char *config_path (void) { return g_path; }

const char *
config_get_string (const char *key, const char *def)
{
    char *v = g_key_file_get_string (g_kf, "paint-c", key, NULL);
    if (!v) return def;
    static char *ret = NULL;
    g_free (ret);
    ret = v;
    return ret;
}

int
config_get_int (const char *key, int def)
{
    GError *e = NULL;
    int v = g_key_file_get_integer (g_kf, "paint-c", key, &e);
    if (e) { g_error_free (e); return def; }
    return v;
}

bool
config_get_bool (const char *key, bool def)
{
    GError *e = NULL;
    gboolean v = g_key_file_get_boolean (g_kf, "paint-c", key, &e);
    if (e) { g_error_free (e); return def; }
    return v;
}

void config_set_string (const char *key, const char *val)
{ if (val) g_key_file_set_string (g_kf, "paint-c", key, val); }
void config_set_int    (const char *key, int v)
{ g_key_file_set_integer (g_kf, "paint-c", key, v); }
void config_set_bool   (const char *key, bool v)
{ g_key_file_set_boolean (g_kf, "paint-c", key, v); }

void
config_save (void)
{
    if (!g_kf || !g_path) return;
    GError *err = NULL;
    if (!g_key_file_save_to_file (g_kf, g_path, &err)) {
        LOG_WARN ("config save: %s", err ? err->message : "?");
        if (err) g_error_free (err);
    } else {
        LOG_INFO ("config saved: %s", g_path);
    }
}
