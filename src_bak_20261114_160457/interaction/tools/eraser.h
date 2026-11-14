/* eraser.h — writes transparent pixels into the active layer.
 * LAYER: L3.
 *
 * TODO (M3.5): pressure-scaled soft edge.
 */

#pragma once
#include "../tool.h"

Tool *eraser_tool_new        (void);
void  eraser_tool_set_radius (Tool *tool, int radius);
int   eraser_tool_get_radius (Tool *tool);
