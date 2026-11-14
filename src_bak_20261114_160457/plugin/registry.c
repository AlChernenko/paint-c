#include "registry.h"
#include "../util/log.h"
#include <string.h>

typedef struct { GModule *mod; const PaintPluginDescriptor *desc; } Entry;
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
