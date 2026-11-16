#include "tool_engine.h"
#include "transform.h"
#include "../util/log.h"
#include "../domain/document.h"

static Tool *g_active_tool = NULL;
static Document *g_active_doc = NULL;

static gboolean is_transform_tool(Tool *t) {
    if (!t || !t->vtable) return FALSE;
    return g_strcmp0(t->vtable->id, "move") == 0 || g_strcmp0(t->vtable->id, "rotate") == 0;
}

void tool_engine_set_active (Tool *tool, Document *doc) {
    if (g_active_tool == tool) return;
    if (g_active_tool && g_active_tool->vtable->deactivate)
        g_active_tool->vtable->deactivate(g_active_tool, doc);
    g_active_tool = tool;
    g_active_doc = doc;
    g_active_doc = doc;
    if (tool && tool->vtable->activate)
        tool->vtable->activate(tool, doc);
}
Tool *tool_engine_get_active (void) { return g_active_tool; }

void tool_engine_dispatch_begin (Document *doc, InputEvent *ev) {
    if (!g_active_tool) return;
    
    // SAFETY: If a non-transform tool starts drawing, commit any active transform first.
    // This prevents the previous transform's backup buffers from overwriting new strokes.
    if (!is_transform_tool(g_active_tool)) {
        LOG_DEBUG("Commit before draw (tool: %s)", g_active_tool->vtable->id);
        transform_commit(doc);
    }
    
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

void tool_free(Tool *t) {
    if (!t) return;
    g_free(t->user_data);
    g_free(t);
}

Document *tool_engine_get_document (void) { return g_active_doc; }
