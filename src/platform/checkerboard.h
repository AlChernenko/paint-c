#pragma once
#include <gtk/gtk.h>

/* Returns a cached 16×16 checkerboard texture. Lazy-initialised, never freed. */
GdkTexture *checkerboard_texture (void);
