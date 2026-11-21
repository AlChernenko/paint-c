/* effects.h — one-shot image effects applied to the active layer.
 *
 * LAYER: L2 (domain). No GTK.
 *
 * All effects honour the current selection: if a selection exists only the
 * selected rectangle is modified (same clipping rules as document_apply_*).
 * Each effect wraps itself in a single undoable history action.
 */

#pragma once

#include "document.h"

typedef enum {
    EFFECT_GRAYSCALE = 0,   /* luminance black & white      */
    EFFECT_INVERT,          /* photographic negative        */
    EFFECT_SEPIA,           /* warm duotone                 */
    EFFECT_BRIGHTNESS,      /* factor >1 lighter, <1 darker */
    EFFECT_CONTRAST,        /* factor around pivot 128      */
    EFFECT_GAUSSIAN_BLUR,   /* fixed-radius box blur x3     */
    EFFECT_NOISE,           /* random per-channel grain     */
    EFFECT_COUNT
} PncEffect;

const char *pnc_effect_name (PncEffect e);   /* undo-label / UI text */

/* Apply `effect` with parameters:
 *   strength ∈ [0..1]  – amount of noise / deviation from identity
 *   factor  ∈ [0.5..2] – multiplier for brightness/contrast (ignored else)
 * Returns TRUE if anything was changed. */
gboolean pnc_effect_apply (Document *doc, PncEffect effect,
                           float strength, float factor);
