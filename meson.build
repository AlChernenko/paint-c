# Paint — modular raster editor
# Build: meson setup build && meson compile -C build
#
# To add a dependency: add to the `deps` list below and it is available
# in every src/ compilation unit.

project('paint-c', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',   # -Wall -Wextra; bump to 3 for -Wpedantic
    'werror=false',      # set to true in CI to fail on warnings
  ],
)

# ── Dependencies ──────────────────────────────────────────────────────────
# GTK 4.6 is the minimum for gdk_memory_texture_new() and GtkGestureStylus.
# Do NOT include libpng/libjpeg directly — go through GdkPixbuf in later
# milestones so the tree stays portable.
gtk_dep    = dependency('gtk4', version: '>=4.6')
glib_dep   = dependency('glib-2.0', version: '>=2.72')
gobject_dep = dependency('gobject-2.0')
gmodule_dep = dependency('gmodule-2.0')

# Link the math library explicitly for sqrtf/powf/etc.
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

deps = [gtk_dep, glib_dep, gobject_dep, gmodule_dep, m_dep]

subdir('src')