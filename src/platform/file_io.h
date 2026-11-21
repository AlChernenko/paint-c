/* file_io.h — image/document I/O.
 *
 * LAYER: L1 (platform). GdkPixbuf is a GTK dependency we already pull in.
 *
 * Formats:
 *   - PNG / JPEG / WebP: flattened composite export + single-layer import,
 *     all through GdkPixbuf loaders (no direct libpng/libjpeg deps).
 *   - PDF: multi-page export via cairo's PDF surface (one page per visible
 *     layer, plus a final composited page). Import not supported.
 *   - .pncimg: paint-c native document container — lossless, stores every
 *     layer with its name, opacity, blend mode and visibility, so a saved
 *     project can be re-opened and edited.
 *
 * TODO (M5.5): move to GTask for async I/O — currently synchronous, which
 * freezes the UI on very large files (>50 MB).
 */

#pragma once

#include <glib.h>
#include "../domain/document.h"

typedef enum {
    PNC_FORMAT_PNG,
    PNC_FORMAT_JPEG,
    PNC_FORMAT_WEBP,
    PNC_FORMAT_PDF,
    PNC_FORMAT_PNCIMG,
} PncFormat;

/* Guess a save format from the file extension (case-insensitive).
 * Returns FALSE for unknown extensions. */
gboolean   file_io_format_from_path (const char *path, PncFormat *out);

/* Save the current composite of `doc` in `format`.
 * Returns TRUE on success; sets *error otherwise. */
gboolean   file_io_save         (Document *doc, const char *path,
                                 PncFormat format, GError **error);

/* Convenience wrappers (kept for existing callers/tests). */
gboolean   file_io_save_png     (Document *doc, const char *path, GError **error);

/* Load `path` into a fresh Document. For .pncimg this restores all layers
 * and their settings; for raster formats a single "Background" layer is
 * created. Returns NULL on error; sets *error. Caller owns the Document. */
Document  *file_io_open         (const char *path, GError **error);
Document  *file_io_open_png     (const char *path, GError **error); /* alias */
