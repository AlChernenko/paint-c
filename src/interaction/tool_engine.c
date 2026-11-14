#include "tool_engine.h"

static Tool *g_active_tool = NULL;

void
tool_engine_set_active (Tool *tool)
{
    /* TODO (M7): deactivate old / activate new, and stop being a singleton. */
    g_active_tool = tool;
}

Tool *
tool_engine_get_active (void)
{
    return g_active_tool;
}

void
tool_engine_dispatch_begin (Document *doc, InputEvent *ev)
{
    if (g_active_tool && g_active_tool->vtable->stroke_begin)
        g_active_tool->vtable->stroke_begin (g_active_tool, doc, ev);
}

void
tool_engine_dispatch_move (Document *doc, InputEvent *ev)
{
    if (g_active_tool && g_active_tool->vtable->stroke_move)
        g_active_tool->vtable->stroke_move (g_active_tool, doc, ev);
}

void
tool_engine_dispatch_end (Document *doc, InputEvent *ev)
{
    if (g_active_tool && g_active_tool->vtable->stroke_end)
        g_active_tool->vtable->stroke_end (g_active_tool, doc, ev);
}