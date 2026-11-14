/* input_event.h — the only thing tools ever see from the input system.
 *
 * LAYER: L3. No GTK dependency so tools can be tested from plain C.
 *
 * Coordinates are in IMAGE space (document pixels, top-left origin).
 * The canvas is responsible for the screen→image conversion; tools never
 * see screen coordinates. This is architecture §5.2, rule 1.
 */

#pragma once

typedef struct {
    /* Image-space position. Fractional values are valid — brush radius
     * rounding happens in the tool, not here. */
    double x, y;

    /* Normalized 0.0 .. 1.0. Always 1.0 for mouse input. */
    double pressure;

    /* Tablet axes — all 0.0 when absent. */
    double tilt_x, tilt_y;
    double rotation;
    double distance;

    /* Reserved for modifier keys — currently unused. Fill in from the
     * GdkModifierType on the triggering event when M5+ needs it. */
    unsigned modifiers;
} InputEvent;