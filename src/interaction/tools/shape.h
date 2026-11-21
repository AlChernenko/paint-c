/* shape.h — parametric shape drawing tool.
 *
 * Shapes are drawn as a live preview on the active layer while dragging;
 * the result is only committed to history on stroke_end (or discarded by
 * Escape via tool_engine cancel). See shape.c for the anti-aliased
 * coverage rasterizer.
 */
#pragma once
#include "../tool.h"

Tool *shape_tool_new (void);

/* Shape kinds exposed in the options UI (index == value). */
typedef enum { SHAPE_RECT = 0, SHAPE_ELLIPSE, SHAPE_TRIANGLE, SHAPE_STAR, SHAPE_COUNT } ShapeKind;

/* Keep the shape colour in sync with the palette / eyedropper. */
void  shape_tool_set_color (Tool *tool, Pixel color);

/* Commit or discard an in-progress shape gesture.
 * commit = TRUE  → finalise at `to` (same as releasing the mouse).
 * commit = FALSE → restore the pre-drag snapshot (Escape / cancel).
 * Safe to call when no gesture is active (no-op). */
void  shape_tool_commit_or_cancel (Tool *tool, gboolean commit,
                                   double to_x, double to_y);
