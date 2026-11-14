# Paint C — Developer Documentation

This document describes the internal architecture, data flow, and systems of Paint C.

## 1. System Architecture

The application is structured into four distinct layers, enforcing a strict downward dependency rule:

- L4 — Application Shell (src/shell/): UI composition, GTK widgets, menus, and dialogs. Owns the PaintWindow and responds to user input.
- L3 — Interaction (src/interaction/): Tool logic (ToolVTable), keyboard/mouse routing (tool_engine.c), and event normalization (input_event.h).
- L2 — Domain (src/domain/): The core engine. Contains zero GTK dependencies. Includes Document, Layer, PixelBuffer, Compositor, and the CommandStack.
- L1 — Platform (src/platform/): Bridges to OS/GTK. Handles GdkTexture uploads (canvas.c), file I/O (file_io.c), and system clipboard (clipboard.c).

## 2. The Document Model (L2)

The Document object is the single source of truth. It is an opaque struct.
- Layers: An array of Layer pointers. Each layer contains a PixelBuffer.
- PixelBuffer: A simple flat array of Pixel structs (BGRA, straight alpha).
- Observers: To maintain separation, the Document uses a linked list of DocumentObserver callbacks. When a layer changes, document_invalidate() composites the layers and notifies the observers (e.g., the Canvas redraws, the History panel updates).

## 3. Rendering Pipeline

1. When a tool modifies pixels, it calls document_apply_*() functions.
2. These functions write directly to the Layer's PixelBuffer.
3. If a change occurs, the Document is marked dirty, and document_invalidate() is called.
4. document_invalidate() runs the Compositor, which loops through all visible layers (bottom to top) and blends them into a master composite buffer.
5. The Canvas widget (canvas.c) observes this change. It converts the straight-alpha composite buffer to premultiplied BGRA, wraps it in a GBytes, and uploads it to a GdkMemoryTexture.
6. GTK renders the texture via gtk_snapshot_append_texture().

## 4. The History System (Undo/Redo)

The history system uses the Command Pattern.

- Every action (stroke, layer add, move) creates a Command struct.
- Strokes: Handled by document_begin_stroke() and document_end_stroke(). When a stroke ends, the system calculates the bounding box of the changed pixels. It saves a "before" crop and an "after" crop into a StrokeCmd.
- Regions (Paste/Clear): Handled by RegionCmd, which saves the rectangular bounds before and after the operation.
- Layer Ops: Handled by LayerCmd, which stores the Layer pointer and its index.
- Memory: The CommandStack keeps a running total of memory used. If it exceeds the limit (256MB), it silently discards the oldest commands at the bottom of the stack.

## 5. The Tool System (L3)

All tools implement the ToolVTable (in tool.h):
typedef struct {
    const char *id, *name, *icon_name;
    void (*activate)(Tool*, Document*);
    void (*deactivate)(Tool*, Document*);
    void (*stroke_begin)(Tool*, Document*, InputEvent*);
    void (*stroke_move)(Tool*, Document*, InputEvent*);
    void (*stroke_end)(Tool*, Document*, InputEvent*);
    GtkWidget *(*build_options_ui)(Tool*);
} ToolVTable;

- Tool Engine: The tool_engine.c file is a singleton that holds the currently active tool. It receives normalized InputEvents (which have already been translated from screen to image coordinates) and routes them to the tool.
- Transform Tool: This tool temporarily "floats" pixels. When activated, it copies the selected region, punches a transparent hole, and begins a stroke. While dragging, it restores the layer from a backup buffer, re-punches the hole, and pastes the floating pixels at the new location.

## 6. UI & Input Routing

- Layout: The main window uses nested GtkPaned widgets to create resizable left and right sidebars. The central canvas is wrapped in a GtkScrolledWindow containing a centering GtkBox.
- Keyboard: A GtkEventControllerKey is attached to the main window at GTK_PHASE_CAPTURE. This guarantees that all keyboard shortcuts (Ctrl+Z, P, M, etc.) are intercepted before any other widget (like a text entry or spin button) can steal focus.
- Mouse: The canvas uses GtkGestureStylus for tablet input (pressure/tilt) and a GtkGestureClick + GtkEventControllerMotion for standard mouse input. Middle-click triggers panning. Right-click triggers the color picker.

## 7. Plugin System (Future)

The app is designed to load external tools via GModule. A plugin must export a PaintPluginDescriptor and implement the ToolVTable. The plugin_registry.c scans the addons/ directory at startup and registers them dynamically.
