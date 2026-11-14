#include "history.h"
#include <string.h>

Command *command_new (const char *label, gsize bytes) {
    Command *c = g_new0 (Command, 1);
    c->label = g_strdup (label ? label : "Action");
    c->bytes = bytes;
    return c;
}
void command_free (Command *c) {
    if (!c) return;
    if (c->free) c->free (c);
    g_free (c->label);
    g_free (c);
}
void command_stack_init (CommandStack *s) {
    memset (s, 0, sizeof *s);
    s->max_bytes = 256 * 1024 * 1024;
}
void command_stack_clear (CommandStack *s) {
    Command *c = s->head;
    while (c) { Command *n = c->next; command_free (c); c = n; }
    s->head = s->current = NULL;
    s->total_bytes = 0;
}
void command_stack_push (CommandStack *s, Command *c) {
    if (s->suppress || !c) { if (c && s->suppress) command_free(c); return; }

    Command *after = s->current ? s->current->next : s->head;
    while (after) {
        Command *n = after->next;
        s->total_bytes -= after->bytes;
        command_free (after);
        after = n;
    }
    if (s->current) s->current->next = NULL;
    else            s->head = NULL;

    c->prev = s->current;
    c->next = NULL;
    if (s->current) s->current->next = c;
    else            s->head = c;
    s->current = c;
    s->total_bytes += c->bytes;

    while (s->total_bytes > s->max_bytes && s->head && s->head != s->current) {
        Command *old = s->head;
        s->head = old->next;
        if (s->head) s->head->prev = NULL;
        s->total_bytes -= old->bytes;
        command_free (old);
    }
}
gboolean command_stack_can_undo (const CommandStack *s) { return s->current != NULL; }
gboolean command_stack_can_redo (const CommandStack *s) { return s->current ? s->current->next != NULL : s->head != NULL; }
gboolean command_stack_undo (CommandStack *s, Document *doc) {
    if (!s->current) return FALSE;
    Command *c = s->current;
    s->suppress = TRUE;
    if (c->undo) c->undo (c, doc);
    s->suppress = FALSE;
    s->current = c->prev;
    return TRUE;
}
gboolean command_stack_redo (CommandStack *s, Document *doc) {
    Command *next = s->current ? s->current->next : s->head;
    if (!next) return FALSE;
    s->suppress = TRUE;
    if (next->redo) next->redo (next, doc);
    s->suppress = FALSE;
    s->current = next;
    return TRUE;
}
const char *command_stack_undo_label (const CommandStack *s) { return s->current ? s->current->label : NULL; }
const char *command_stack_redo_label (const CommandStack *s) {
    Command *next = s->current ? s->current->next : s->head;
    return next ? next->label : NULL;
}
int command_stack_length (const CommandStack *s) {
    int n = 0; Command *c = s->head;
    while (c) { n++; c = c->next; }
    return n;
}
Command *command_stack_get (const CommandStack *s, int index) {
    Command *c = s->head;
    while (c && index > 0) { c = c->next; index--; }
    return c;
}
int command_stack_current_index (const CommandStack *s) {
    if (!s->current) return -1;
    int i = 0; Command *c = s->head;
    while (c && c != s->current) { c = c->next; i++; }
    return i;
}
