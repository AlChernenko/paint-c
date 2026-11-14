/* pencil.h — the first built-in tool.
 *
 * LAYER: L3. Very simple by design — it exists to prove the whole pipeline:
 *   stylus event → InputEvent → tool_engine → document_apply_dot →
 *   compositor → observer → canvas texture → on-screen.
 *
 * TODO (M2.5): interpolate between successive stylus positions with
 *               Bresenham so fast strokes don't leave gaps. See
 *               pencil_stroke_move().
 * TODO (M3): switch from a filled square to a pressure-scaled radial stamp
 *             (see DevDoc §5.2 pseudocode for the brush tool).
 * TODO (M7): expose this as a plugin by moving the factory into
 *             plugins/pencil/ and dropping the registration into
 *             plugin_registry_scan().
 */

#pragma once

#include "../tool.h"

void pencil_tool_set_color  (Tool *tool, Pixel color);
void pencil_tool_set_radius (Tool *tool, int radius);
int  pencil_tool_get_radius (Tool *tool);

Tool *pencil_tool_new (void);