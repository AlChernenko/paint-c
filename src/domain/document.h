#pragma once
#include "layer.h"

typedef struct Document Document;
typedef struct _DocumentObserverNode *DocumentObserverHandle;
typedef struct { int x, y, w, h; } SelectionRect;

typedef struct {
    void (*on_composite_changed)    (void *user_data);
    void (*on_layer_list_changed)   (void *user_data);
    void (*on_active_layer_changed) (void *user_data);
    void (*on_selection_changed)    (void *user_data);
    void (*on_history_changed)      (void *user_data);
} DocumentObserver;

Document *document_new  (int width, int height);
void      document_free (Document *doc);

int            document_width        (const Document *doc);
int            document_height       (const Document *doc);
int            document_layer_count  (const Document *doc);
Layer         *document_layer_at     (const Document *doc, int idx);
int            document_active_layer (const Document *doc);
const uint8_t *document_composite    (const Document *doc);
bool           document_is_dirty     (const Document *doc);

Layer *document_add_layer         (Document *doc, const char *name);
Layer *document_duplicate_layer   (Document *doc, int idx);
bool   document_remove_layer      (Document *doc, int idx);
void   document_move_layer        (Document *doc, int from, int to);
bool   document_merge_down        (Document *doc, int idx);
void   document_set_active_layer  (Document *doc, int idx);
void   document_layer_set_visible    (Document *doc, int idx, bool visible);
void   document_layer_set_opacity    (Document *doc, int idx, float opacity);
void   document_layer_set_blend_mode (Document *doc, int idx, BlendMode mode);

void document_apply_dot   (Document *doc, int cx, int cy, Pixel color, int radius);
void document_apply_erase (Document *doc, int cx, int cy, int radius);
void document_apply_blur  (Document *doc, int cx, int cy, int radius);
void document_apply_stroke       (Document *doc, int x0, int y0, int x1, int y1, Pixel color, int radius);
void document_apply_erase_stroke (Document *doc, int x0, int y0, int x1, int y1, int radius);
void document_apply_blur_stroke  (Document *doc, int x0, int y0, int x1, int y1, int radius);

void document_invalidate (Document *doc);

void document_begin_stroke (Document *doc, const char *action_name);
void document_end_stroke   (Document *doc);
bool document_undo (Document *doc);
bool document_redo (Document *doc);
const char *document_undo_label (const Document *doc);
const char *document_redo_label (const Document *doc);
int  document_history_length (const Document *doc);
const char *document_history_label_at (const Document *doc, int idx);
int  document_history_current_index (const Document *doc);

void   document_mark_clean      (Document *doc);
bool   document_has_selection   (const Document *doc);
SelectionRect document_selection       (const Document *doc);
void   document_set_selection   (Document *doc, int x, int y, int w, int h);
void   document_clear_selection (Document *doc);
PixelBuffer *document_copy_region (Document *doc);
void   document_paste_buffer    (Document *doc, const PixelBuffer *buf, int x, int y);
void   document_paste_as_layer  (Document *doc, const PixelBuffer *buf, const char *name);
void   document_clear_region    (Document *doc);

DocumentObserverHandle document_add_observer    (Document *doc, const DocumentObserver *obs, void *user_data);
void                   document_remove_observer (Document *doc, DocumentObserverHandle h);
void document_clear_history (Document *doc);
