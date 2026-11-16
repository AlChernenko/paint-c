#pragma once
#include "../domain/document.h"
#include <glib.h>

typedef struct {
    gboolean active;
    PixelBuffer *backup_layer;
    PixelBuffer *orig_floating;   // Pristine copy from selection
    PixelBuffer *base_floating;   // Resized (unrotated) copy
    PixelBuffer *floating;        // Final rotated/flipped copy

    int layer_w, layer_h;               // Canvas dimensions at transform start
    int hole_x, hole_y, hole_w, hole_h; // Original location
    int cur_x, cur_y;                   // Current visual top-left
    int base_w, base_h;                 // Unrotated dimensions
    int cur_w, cur_h;                   // Rotated bounding box dimensions

    double angle;
    gboolean flip_h, flip_v;
    double ref_center_x, ref_center_y;  // Center of the transform
} TransformContext;

TransformContext *transform_get(void);
void transform_begin(Document *doc);
void transform_update(Document *doc);
void transform_commit(Document *doc);
void transform_reset(void);
