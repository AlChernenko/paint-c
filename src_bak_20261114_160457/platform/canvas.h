#pragma once
#include <gtk/gtk.h>
#include "../domain/document.h"

#define PAINT_TYPE_CANVAS (paint_canvas_get_type ())
G_DECLARE_FINAL_TYPE (PaintCanvas, paint_canvas, PAINT, CANVAS, GtkWidget)

GtkWidget  *paint_canvas_new          (void);
void        paint_canvas_set_document (PaintCanvas *canvas, Document *doc);
Document   *paint_canvas_get_document (PaintCanvas *canvas);

double      paint_canvas_get_zoom     (PaintCanvas *canvas);
void        paint_canvas_set_zoom     (PaintCanvas *canvas, double zoom);
void        paint_canvas_zoom_in      (PaintCanvas *canvas);
void        paint_canvas_zoom_out     (PaintCanvas *canvas);
void        paint_canvas_zoom_fit     (PaintCanvas *canvas, int vw, int vh);

/* New APIs for this drop */
void paint_canvas_set_scroll_adjustments (PaintCanvas *canvas,
                                           GtkAdjustment *hadj,
                                           GtkAdjustment *vadj);
void paint_canvas_set_cursor_radius     (PaintCanvas *canvas,
                                          double radius_image_px);
