#pragma once
#include "plugin_api.h"

void        plugin_registry_init      (const char *addons_dir);
int         plugin_registry_count     (void);
Tool       *plugin_registry_create    (int idx);   /* fresh Tool* each call */
const char *plugin_registry_name      (int idx);
const char *plugin_registry_id        (int idx);
void        plugin_registry_cleanup   (void);
