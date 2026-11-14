# Paint C

A fast, lightweight raster image editor written in pure C using GTK4. Inspired by the classic Paint.NET, it aims to provide a clean, native desktop experience with a powerful plugin system.

## Features

- **Multi-Layer Canvas:** Full compositing engine with support for Normal, Multiply, and Screen blend modes.
- **Undo/Redo:** Robust command-stack history system with a 256MB memory budget.
- **Transform Tool:** Move, resize, and scale selected pixels with bilinear interpolation for smooth results.
- **Selection:** Rectangular selection with visible handles for resizing and dragging.
- **System Clipboard:** Seamless Copy/Cut/Paste with external apps (GIMP, browsers, etc.).
- **Drawing Tools:** Pencil, Eraser, Line, Blur, Eyedropper.
- **Zoom & Pan:** Smooth zooming with Ctrl+Wheel or keyboard shortcuts, and middle-mouse panning.
- **Dynamic UI:** Resizable dockable sidebars (GtkPaned) for Tools, Colors, History, and Layers.
- **Plugins:** Runtime loadable .so/.dll addons for custom tools.
- **Native Config:** Settings and window state persist across sessions.

## Installation & Build

### Prerequisites
- C11 Compiler (GCC or Clang)
- Meson (>= 0.60)
- Ninja
- GTK4 (>= 4.6)
- GLib (>= 2.72)

### Debian / Ubuntu
sudo apt install build-essential meson ninja-build libgtk-4-dev

### Fedora
sudo dnf install gcc meson ninja-build gtk4-devel

### Arch Linux
sudo pacman -S base-devel meson ninja gtk4

### Windows (MSYS2)
Open the MSYS2 UCRT64 terminal (not the default MSYS shell):
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-gtk4

### Build Steps
git clone https://github.com/yourname/paint-c.git
cd paint-c
meson setup build
meson compile -C build

### Run
Linux:
./build/src/paint-c

Windows (from MSYS2 UCRT64 terminal):
./build/src/paint-c.exe

## Keyboard Shortcuts

### Tools
P - Pencil
E - Eraser
S - Select
M - Transform (Move)
L - Line
B - Blur
I - Eyedropper

### Edit & View
Ctrl + Z - Undo
Ctrl + Shift + Z - Redo
Ctrl + X - Cut
Ctrl + C - Copy
Ctrl + V - Paste
Ctrl + A - Select All
Ctrl + D - Deselect
Delete - Delete Selection
Ctrl + = - Zoom In
Ctrl + - - Zoom Out
Ctrl + 0 - Zoom to Fit
Ctrl + Mouse Wheel - Zoom under cursor

## Plugins
Drop compiled .so or .dll files into the build/addons/ directory and restart the app. See addons/README.md for API documentation.

## License
MIT License
