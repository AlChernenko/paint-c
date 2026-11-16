#pragma once
#include "plugin_api.h"

void        plugin_registry_init      (const char *addons_dir);
int         plugin_registry_count     (void);
Tool       *plugin_registry_create    (int idx);   /* fresh Tool* each call */
const char *plugin_registry_name      (int idx);
const char *plugin_registry_id        (int idx);
bool        plugin_registry_get_enabled (int idx);
void        plugin_registry_set_enabled (int idx, bool enabled);
const PaintPluginDescriptor *plugin_registry_get_desc (int idx);
bool plugin_registry_get_loaded (int idx);
bool plugin_registry_needs_restart (int idx);
void plugin_registry_set_needs_restart (int idx, bool val);
bool plugin_registry_any_pending_restart (void);
void        plugin_registry_cleanup   (void);
