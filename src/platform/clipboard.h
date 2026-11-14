/* clipboard.h — internal + system clipboard. LAYER: L1. */

#pragma once
#include <stdbool.h>
#include "../domain/pixel_buffer.h"

typedef void (*ClipboardPasteFn) (PixelBuffer *buf, void *user_data);

/* Internal clipboard (fast path, same-process). */
void         clipboard_set           (const PixelBuffer *buf);
bool         clipboard_has_internal  (void);
PixelBuffer *clipboard_get_internal  (void);   /* caller frees; NULL if empty */

/* System clipboard (GdkClipboard interop). Fire-and-forget. */
void         clipboard_set_system    (const PixelBuffer *buf);

/* Async read from system clipboard. cb gets NULL on failure; buf is freed
 * immediately after cb returns, so copy it if you want to keep it. */
void         clipboard_get_system_async (ClipboardPasteFn cb, void *user_data);
