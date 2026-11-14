#include "tool_engine.h"
#include "../domain/document.h"

static Tool *g_active_tool = NULL;

void tool_engine_set_active (Tool *tool, Document *doc) {
    if (g_active_tool == tool) return;
    if (g_active_tool && g_active_tool->vtable->deactivate)
        g_active_tool->vtable->deactivate(g_active_tool, doc);
    g_active_tool = tool;
    if (tool && tool->vtable->activate)
        tool->vtable->activate(tool, doc);
}
Tool *tool_engine_get_active (void) { return g_active_tool; }

void tool_engine_dispatch_begin (Document *doc, InputEvent *ev) {
    if (!g_active_tool) return;
    document_begin_stroke(doc, g_active_tool->vtable->name);
    if (g_active_tool->vtable->stroke_begin)
        g_active_tool->vtable->stroke_begin (g_active_tool, doc, ev);
    document_invalidate(doc); /* Redraw once */
}

void tool_engine_dispatch_move (Document *doc, InputEvent *ev) {
    if (!g_active_tool) return;
    if (g_active_tool->vtable->stroke_move)
        g_active_tool->vtable->stroke_move (g_active_tool, doc, ev);
    document_invalidate(doc); /* Redraw once per mouse event */
}

void tool_engine_dispatch_end (Document *doc, InputEvent *ev) {
    if (!g_active_tool) return;
    if (g_active_tool->vtable->stroke_end)
        g_active_tool->vtable->stroke_end (g_active_tool, doc, ev);
    document_end_stroke(doc);
    document_invalidate(doc);
}
