/* file_io.h — PNG load/save via GdkPixbuf.
 *
 * LAYER: L1 (platform). GdkPixbuf is a GTK dependency we already pull in.
 *
 * TODO (M5.5): move to GTask for async I/O — currently synchronous, which
 * freezes the UI on very large files (>50 MB). Acceptable for M6.
 * TODO (M6+): expose these functions through the ImageFormatVTable so
 * additional formats can be plugged in without touching this file.
 */

#pragma once

#include <glib.h>
#include "../domain/document.h"

/* Save the current composite of `doc` as a PNG.
 * Returns TRUE on success; sets *error otherwise. */
gboolean   file_io_save_png   (Document *doc, const char *path, GError **error);

/* Load `path` into a fresh Document with one background layer.
 * Returns NULL on error; sets *error. The caller owns the returned Document. */
Document  *file_io_open_png   (const char *path, GError **error);