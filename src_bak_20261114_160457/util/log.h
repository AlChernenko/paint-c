/* log.h — minimal leveled logger.
 *
 * Usage:
 *   LOG_ERR ("failed: %s", msg);     // always shown
 *   LOG_WARN (...);                  // always shown
 *   LOG_INFO (...);                  // shown unless PAINT_DEBUG=0
 *   LOG_DEBUG (...);                 // only with PAINT_DEBUG=debug (or any unrecognized value)
 *
 * Environment:
 *   PAINT_DEBUG=0|off     -> errors only
 *   PAINT_DEBUG=warn      -> errors + warnings
 *   PAINT_DEBUG=info      -> default
 *   PAINT_DEBUG=debug     -> everything
 */

#pragma once
#include <glib.h>

void paint_log_init (void);
void paint_log_msg  (int level, const char *fmt, ...) G_GNUC_PRINTF (2, 3);

#define LOG_ERR(...)   paint_log_msg (0, __VA_ARGS__)
#define LOG_WARN(...)  paint_log_msg (1, __VA_ARGS__)
#define LOG_INFO(...)  paint_log_msg (2, __VA_ARGS__)
#define LOG_DEBUG(...) paint_log_msg (3, __VA_ARGS__)
