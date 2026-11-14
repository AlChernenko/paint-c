/* swatch.h — a small clickable colour square.
 * Replaces the CSS-provider-per-swatch bug that turned every button magenta.
 */
#pragma once
#include <gtk/gtk.h>

#define PAINT_TYPE_SWATCH (paint_swatch_get_type ())
G_DECLARE_FINAL_TYPE (PaintSwatch, paint_swatch, PAINT, SWATCH, GtkWidget)

GtkWidget *paint_swatch_new        (const GdkRGBA *color);
void       paint_swatch_set_color  (PaintSwatch *self, const GdkRGBA *color);
void       paint_swatch_get_color  (PaintSwatch *self, GdkRGBA *out);
