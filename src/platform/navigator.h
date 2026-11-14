#pragma once
#include <gtk/gtk.h>
#include "../domain/document.h"

#define PAINT_TYPE_NAVIGATOR (paint_navigator_get_type ())
G_DECLARE_FINAL_TYPE (PaintNavigator, paint_navigator, PAINT, NAVIGATOR, GtkWidget)

GtkWidget *paint_navigator_new          (void);
void       paint_navigator_set_document (PaintNavigator *nav, Document *doc);
void       paint_navigator_set_scroll   (PaintNavigator *nav,
                                          GtkAdjustment *hadj,
                                          GtkAdjustment *vadj,
                                          double zoom);
