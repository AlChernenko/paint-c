#include "log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int g_level = 2;   /* default: info */

void
paint_log_init (void)
{
    const char *env = g_getenv ("PAINT_DEBUG");
    if (!env) return;
    if (strcmp (env, "0") == 0 || strcmp (env, "off") == 0)  g_level = -1;
    else if (strcmp (env, "error") == 0) g_level = 0;
    else if (strcmp (env, "warn")  == 0) g_level = 1;
    else if (strcmp (env, "info")  == 0) g_level = 2;
    else                                 g_level = 3;  /* any other value -> debug */
    fprintf (stderr, "[paint] log level = %d\n", g_level);
}

void
paint_log_msg (int level, const char *fmt, ...)
{
    if (level > g_level) return;
    static const char *names[] = { "ERR ", "WARN", "INFO", "DBG " };
    va_list ap;
    fprintf (stderr, "[%s] ", names[level]);
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    fputc ('\n', stderr);
}
