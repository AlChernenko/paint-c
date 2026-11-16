#include "registry.h"
#include "../util/log.h"
#include "../util/config.h"
#include <string.h>

typedef struct { GModule *mod; const PaintPluginDescriptor *desc; bool enabled; bool loaded; bool needs_restart; } Entry;
static Entry *g_entries = NULL;
static int    g_count   = 0;
static int    g_cap     = 0;

void
plugin_registry_init (const char *dir)
{
    if (!dir) return;
    GDir *d = g_dir_open (dir, 0, NULL);
    if (!d) { LOG_WARN ("plugin dir not found: %s", dir); return; }

    const char *name;
    while ((name = g_dir_read_name (d))) {
        if (!g_str_has_suffix (name, ".so") && !g_str_has_suffix (name, ".dll"))
            continue;
        char *path = g_build_filename (dir, name, NULL);
        GModule *mod = g_module_open (path, G_MODULE_BIND_LAZY);
        if (!mod) {
            LOG_WARN ("failed to open %s: %s", path, g_module_error ());
            g_free (path);
            continue;
        }
        PaintPluginGetDescriptorFunc get_fn = NULL;
        if (!g_module_symbol (mod, "paint_plugin_get_descriptor", (gpointer *)&get_fn)) {
            LOG_WARN ("%s has no paint_plugin_get_descriptor", path);
            g_module_close (mod);
            g_free (path);
            continue;
        }
        const PaintPluginDescriptor *d = get_fn ();
        if (!d || d->api_version != PAINT_PLUGIN_ABI_VERSION) {
            LOG_WARN ("%s ABI mismatch (have %d, need %d)",
                      path, d ? d->api_version : -1, PAINT_PLUGIN_ABI_VERSION);
            g_module_close (mod);
            g_free (path);
            continue;
        }
        g_module_make_resident (mod);
        if (g_count == g_cap) {
            g_cap = g_cap ? g_cap * 2 : 4;
            g_entries = g_renew (Entry, g_entries, g_cap);
        }
        g_entries[g_count].mod  = mod;
        g_entries[g_count].desc = d;
        char key[256];
        snprintf(key, sizeof key, "addon/%s/enabled", d->id);
        g_entries[g_count].enabled = config_get_bool(key, true);
        snprintf(key, sizeof key, "addon/%s/needs_restart", d->id);
        g_entries[g_count].needs_restart = config_get_bool(key, false);
        g_entries[g_count].loaded = g_entries[g_count].enabled;
        g_count++;
        LOG_INFO ("loaded plugin: %s (%s) from %s", d->name, d->id, name);
        g_free (path);
    }
    g_dir_close (d);
}

int         plugin_registry_count (void) { return g_count; }
const char *plugin_registry_name  (int i) { return (i >= 0 && i < g_count) ? g_entries[i].desc->name : NULL; }
const char *plugin_registry_id    (int i) { return (i >= 0 && i < g_count) ? g_entries[i].desc->id   : NULL; }

Tool *
plugin_registry_create (int i)
{
    if (i < 0 || i >= g_count) return NULL;
    if (!g_entries[i].desc->create_tool) return NULL;
    return g_entries[i].desc->create_tool ();
}

void
plugin_registry_cleanup (void)
{
    /* Modules are resident — do not close. */
    g_free (g_entries);
    g_entries = NULL;
    g_count = g_cap = 0;
}

bool plugin_registry_get_enabled (int i) { return (i >= 0 && i < g_count) ? g_entries[i].enabled : false; }
void plugin_registry_set_enabled (int i, bool enabled) {
    if (i < 0 || i >= g_count) return;
    g_entries[i].enabled = enabled;
    char key[256];
    snprintf(key, sizeof key, "addon/%s/enabled", g_entries[i].desc->id);
    config_set_bool(key, enabled);
    config_save();
    
    if (!enabled) {
        g_entries[i].loaded = false;
    } else {
        if (!g_entries[i].needs_restart) g_entries[i].loaded = true;
    }
}
const PaintPluginDescriptor *plugin_registry_get_desc (int i) { return (i >= 0 && i < g_count) ? g_entries[i].desc : NULL; }

bool plugin_registry_get_loaded (int i) { return (i >= 0 && i < g_count) ? g_entries[i].loaded : false; }
bool plugin_registry_needs_restart (int i) { return (i >= 0 && i < g_count) ? g_entries[i].needs_restart : false; }
void plugin_registry_set_needs_restart (int i, bool val) {
    if (i < 0 || i >= g_count) return;
    g_entries[i].needs_restart = val;
    char key[256];
    snprintf(key, sizeof key, "addon/%s/needs_restart", g_entries[i].desc->id);
    config_set_bool(key, val);
    config_save();
}
bool plugin_registry_any_pending_restart (void) {
    for (int i = 0; i < g_count; i++) if (g_entries[i].enabled && !g_entries[i].loaded) return true;
    return false;
}
