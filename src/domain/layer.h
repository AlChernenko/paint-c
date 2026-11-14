/* layer.h — a single layer of the document.
 *
 * LAYER: L2 (domain). No GTK.
 *
 * TODO (M4): layer name is currently a strdup'd char*. When we introduce
 * the LayerPanel we'll want it to be a GObject with notify signals. For
 * now keep it C-plain so the compositor can stay trivially testable.
 */

#pragma once
#include <stdbool.h>

#include "pixel_buffer.h"

typedef enum {
    BLEND_NORMAL = 0,
    BLEND_MULTIPLY,
    BLEND_SCREEN,
    /* TODO (M4): add OVERLAY, SOFT_LIGHT — keep additions at the end so
     * serialized project files remain compatible. */
} BlendMode;

typedef struct {
    char        *name;        /* owned; free in layer_free() */
    PixelBuffer *buffer;      /* owned; see pixel_buffer_free() */
    float        opacity;     /* 0.0 .. 1.0 */
    BlendMode    blend_mode;
    bool         visible;
} Layer;

Layer *layer_new  (const char *name, int width, int height);
void   layer_free (Layer *layer);