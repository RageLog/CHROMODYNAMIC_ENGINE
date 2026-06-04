# engine/ui/editor/panel_light_editor — cd::editor_panel_light_editor
#
# phase664 — Light Editor panel library.
# Provides cd::editor::panel::light_editor::LightEditor (DrawBatcher path).
# Point-light list (per-light: position/radius/intensity/colour swatch) +
# cluster grid stats (X/Y/Z cells + near/far). Click-to-select highlighting.
#
# Targets:
#   cd_editor_panel_light_editor   (real target)
#   cd::editor_panel_light_editor  (alias)

cd_add_library(editor_panel_light_editor
  TYPE STATIC
  SOURCES
    src/LightEditor.cpp
  PUBLIC_DEPS
    cd::core
    cd::lighting_clusters   # PointLight + ClusterGrid
    cd::ui_renderer         # DrawBatcher
    cd::ui_widgets          # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
