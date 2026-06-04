# engine/ui/editor/panel_debug_viz — cd::editor_panel_debug_viz
#
# phase684 — Debug Visualization overlay library (M13 W6A).
# Provides cd::editor::debug_viz::DebugVizOverlay (DrawBatcher path).
#
# Three toggleable ~200x150 px thumbnails the editor shows in the top-right
# corner for render debugging:
#   kDepth       — grayscale G-buffer depth (closer = brighter).
#   kNormal      — world-normals as RGB (xyz * 0.5 + 0.5).
#   kAlphaBucket — per-pixel render-bucket colour (green/yellow/red).
#
# Sprint-1 placeholder: coloured gradient tiles. Sprint-2: real G-buffer
# sampling when cd::render::framegraph exposes depth/normal/bucket textures.
#
# MOMENT: render dev glances at the three thumbnails, spots the curtain
# is kAlphaBlend (red) → framegraph order confirmed → bug narrows elsewhere.
#
# Targets:
#   cd_editor_panel_debug_viz   (real target)
#   cd::editor_panel_debug_viz  (alias)

cd_add_library(editor_panel_debug_viz
  TYPE STATIC
  SOURCES
    src/DebugViz.cpp
  PUBLIC_DEPS
    cd::core
    cd::rhi          # cd/rhi/Handles.hpp — TextureHandle (phase735)
    cd::ui_renderer  # DrawBatcher
    cd::ui_widgets   # Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
