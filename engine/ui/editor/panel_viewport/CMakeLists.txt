# engine/ui/editor/panel_viewport — cd::editor_panel_viewport
#
# phase546 — Viewport panel library.
# Provides cd::editor::panel::viewport::Viewport (DrawBatcher path).
# Accepts a cd::rhi::TextureHandle representing the scene colour render
# target and emits a kTextured quad via the DrawBatcher.
#
# Targets:
#   cd_editor_panel_viewport   (real target)
#   cd::editor_panel_viewport  (alias)

cd_add_library(editor_panel_viewport
  TYPE STATIC
  SOURCES
    src/Viewport.cpp
  PUBLIC_DEPS
    cd::core
    cd::rhi          # cd/rhi/Handles.hpp — TextureHandle
    cd::ui_renderer  # DrawBatcher
    cd::ui_widgets   # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
