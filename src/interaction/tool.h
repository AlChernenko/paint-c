/* tool.h — the tool interface every tool (built-in or plugin) implements.
 *
 * LAYER: L3. Includes GTK *only* for GtkWidget in build_options_ui. If we
 * ever need to build tools without GTK, split the vtable into two structs.
 *
 * Architectural invariants (architecture §6.2):
 *   - A Tool instance is long-lived per registration, NOT per-stroke.
 *     Transient state (current stroke path) lives in `user_data` and is
 *     reset in stroke_begin.
 *   - Tools MUST mutate the document via document_apply_* only.
 *   - build_options_ui() is called lazily and cached by the shell.
 *
 * ─── WRITING A NEW TOOL ──────────────────────────────────────────────────
 *   1. Fill a static const ToolVTable.
 *   2. Provide an `xxx_tool_new()` factory returning Tool*.
 *   3. Register the factory with tool_engine (M2) or the plugin registry (M7).
 *   4. Add your .c to src/meson.build.
 */

#pragma once

#include <gtk/gtk.h>                /* only for GtkWidget below */
#include "../domain/document.h"
#include "input_event.h"

typedef struct Tool Tool;

typedef struct {
    const char *id;                 /* unique, e.g. "pencil" */
    const char *name;
    const char *description;               /* user-visible, e.g. "Pencil" */
    const char *icon_name;
    const char *group_id;          /* optional, may be NULL */

    void (*activate)  (Tool *tool, Document *doc);
    void (*deactivate)(Tool *tool, Document *doc);

    /* The three stroke phases. Only stroke_begin and stroke_end are
     * mandatory — a tool may ignore motion if it only wants a click. */
    void (*stroke_begin)(Tool *tool, Document *doc, InputEvent *ev);
    void (*stroke_move) (Tool *tool, Document *doc, InputEvent *ev);
    void (*stroke_end)  (Tool *tool, Document *doc, InputEvent *ev);

    /* Optional; return NULL if no UI. */
    GtkWidget *(*build_options_ui)(Tool *tool);
} ToolVTable;

struct Tool {
    const ToolVTable *vtable;
    void             *user_data;    /* tool-owned state */
};