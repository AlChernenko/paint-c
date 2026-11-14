sources = files(
  'main.c',
  'util/log.c',
  'util/config.c',
  'plugin/registry.c',
  'domain/pixel_buffer.c',
  'domain/layer.c',
  'domain/compositor.c',
  'domain/document.c',
  'domain/history.c',
  'platform/canvas.c',
  'platform/clipboard.c',
  'platform/checkerboard.c',
  'platform/navigator.c',
  'platform/file_io.c',                 # NEW
  'interaction/tool_engine.c',
  'interaction/tools/pencil.c',
  'interaction/tools/eraser.c',         # NEW
  'interaction/tools/eyedropper.c',
  'interaction/tools/blur.c',
  'platform/clipboard.c',
  'interaction/tools/select_rect.c',
  'interaction/tools/line.c',
  'interaction/tools/move.c',
  'shell/swatch.c',
  'shell/window.c',
)

executable('paint-c', sources,
  dependencies: deps,
  install: false,
  export_dynamic: true,
)