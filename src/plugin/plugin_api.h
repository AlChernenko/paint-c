/* plugin_api.h — shared between host and addons. ABI-stable.
 *
 * IMPORTANT: This header does NOT redefine Tool/ToolVTable/InputEvent/Pixel.
 * It includes the host headers directly, which guarantees binary compatibility
 * with the host at build time. External plugin .so files must be compiled
 * against THIS file (and the headers it pulls in).
 */

#pragma once
#include <glib.h>
#include <gtk/gtk.h>

/* Host header locations relative to this file: src/plugin/plugin_api.h */
#include "../interaction/tool.h"
#include "../interaction/input_event.h"
#include "../domain/document.h"
#include "../domain/layer.h"
#include "../domain/pixel_buffer.h"

#define PAINT_PLUGIN_ABI_VERSION 2


/* ── Host API (Implemented by the host, callable by addons) ───────── */
Pixel paint_plugin_get_primary_color (void);
void  paint_plugin_get_setting (const char *key, char *buf, int buf_len);
void  paint_plugin_set_setting (const char *key, const char *value);

typedef struct {
    int         api_version;
    const char *id;
    const char *name;
    const char *version;
    const char *author;
    const char *group_id;
    Tool       *(*create_tool)(void);   /* return a fresh Tool* each call */
    GtkWidget  *(*build_settings_ui)(void); /* optional: return a widget for the settings dialog */
} PaintPluginDescriptor;

typedef const PaintPluginDescriptor *(*PaintPluginGetDescriptorFunc)(void);

/* Every addon .so MUST export this function (annotated so plugins can use
 * the same header with G_MODULE_EXPORT on their definition). */
const PaintPluginDescriptor *paint_plugin_get_descriptor (void);
