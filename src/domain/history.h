#pragma once
#include <glib.h>

typedef struct Document Document;
typedef struct Command Command;
struct Command {
    char  *label;
    gsize  bytes;
    void (*undo)(Command *c, Document *doc);
    void (*redo)(Command *c, Document *doc);
    void (*free)(Command *c);
    void  *data;
    Command *prev, *next;
};

typedef struct {
    Command *head;
    Command *current;
    gsize    total_bytes;
    gsize    max_bytes;
    gboolean suppress;
} CommandStack;

Command *command_new  (const char *label, gsize bytes);
void     command_free (Command *c);
void        command_stack_init  (CommandStack *s);
void        command_stack_clear (CommandStack *s);
void        command_stack_push  (CommandStack *s, Command *c);
gboolean    command_stack_can_undo (const CommandStack *s);
gboolean    command_stack_can_redo (const CommandStack *s);
gboolean    command_stack_undo (CommandStack *s, Document *doc);
gboolean    command_stack_redo (CommandStack *s, Document *doc);
const char *command_stack_undo_label (const CommandStack *s);
const char *command_stack_redo_label (const CommandStack *s);

int         command_stack_length (const CommandStack *s);
Command    *command_stack_get (const CommandStack *s, int index);
int         command_stack_current_index (const CommandStack *s);
