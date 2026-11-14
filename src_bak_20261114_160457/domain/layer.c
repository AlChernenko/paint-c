#include "layer.h"
#include <glib.h>

Layer *
layer_new (const char *name, int width, int height)
{
    Layer *l = g_new0 (Layer, 1);
    l->name       = g_strdup (name ? name : "Layer");
    l->buffer     = pixel_buffer_new (width, height);
    l->opacity    = 1.0f;
    l->blend_mode = BLEND_NORMAL;
    l->visible    = true;
    return l;
}

void
layer_free (Layer *l)
{
    if (!l) return;
    g_free (l->name);
    pixel_buffer_free (l->buffer);
    g_free (l);
}