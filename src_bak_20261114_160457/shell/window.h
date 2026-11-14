/* window.h — top-level application window.
 *
 * LAYER: L4 (shell). Composes the canvas + (future) toolbars and panels.
 *
 * TODO (M4): split this into GtkPaned with a layer panel sidebar.
 * TODO (M5): add GtkHeaderBar with a primary menu (Open/Save/Undo).
 */

#pragma once

#include <gtk/gtk.h>

#define PAINT_TYPE_WINDOW (paint_window_get_type ())
G_DECLARE_FINAL_TYPE (PaintWindow, paint_window, PAINT, WINDOW,
                      GtkApplicationWindow)

GtkWidget *paint_window_new (GtkApplication *app);