/* tool_engine.h — global "which tool is active" state + dispatch.
 *
 * LAYER: L3.
 *
 * NOTE: a global singleton is pragmatic for M2. When plugin tools land
 * (M7) this becomes a GObject owned by the window so multiple documents
 * can have different active tools. Keep every call going through these
 * functions so the migration is mechanical.
 *
 * ─── EXTENSION POINTS ────────────────────────────────────────────────────
 *   TODO (M7): tool_engine_register(ToolVTable*) so plugins can add tools.
 *   TODO (M2.5): tool settings persistence via GSettings.
 */

#pragma once

#include "tool.h"

/* Replaces the active tool. Pass NULL to clear.
 * The engine does NOT free the old tool — ownership stays with the caller
 * (usually the window). For M2 the window owns a single pencil tool for
 * the app's whole lifetime. */
void tool_engine_set_active (Tool *tool);

Tool *tool_engine_get_active (void);

/* Called by the canvas. Each safely no-ops if there is no active tool. */
void tool_engine_dispatch_begin (Document *doc, InputEvent *ev);
void tool_engine_dispatch_move  (Document *doc, InputEvent *ev);
void tool_engine_dispatch_end   (Document *doc, InputEvent *ev);