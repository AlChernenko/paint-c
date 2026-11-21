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
