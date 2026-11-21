#include "layer.h"
#include <glib.h>

const char *
blend_mode_name (BlendMode mode)
{
    switch (mode) {
    case BLEND_NORMAL:      return "Normal";
    case BLEND_MULTIPLY:    return "Multiply";
    case BLEND_SCREEN:      return "Screen";
    case BLEND_OVERLAY:     return "Overlay";
    case BLEND_DIFFERENCE:  return "Difference";
    case BLEND_ADDITION:    return "Addition";
    case BLEND_DARKEN:      return "Darken";
    case BLEND_LIGHTEN:     return "Lighten";
    case BLEND_COLOR_DODGE: return "Color Dodge";
    case BLEND_COLOR_BURN:  return "Color Burn";
    }
    return "Normal";
}

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