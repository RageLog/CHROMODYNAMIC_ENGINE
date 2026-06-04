# engine/ui/editor/panel_material_preview — cd::editor_panel_material_preview
#
# phase678 — Material Preview panel library.
# Provides cd::editor::panel::material_preview::MaterialPreview (DrawBatcher path).
# Sprint-1: 2D sphere swatch (base_color tint quad) + metallic/roughness bars +
# alpha cutoff bar (kMask only) + texture path pills.
# Sprint-2 plan: replace sphere quad with real PBR render via cd::material UI
# variant pipeline.
#
# Targets:
#   cd_editor_panel_material_preview   (real target)
#   cd::editor_panel_material_preview  (alias)

cd_add_library(editor_panel_material_preview
  TYPE STATIC
  SOURCES
    src/MaterialPreview.cpp
  PUBLIC_DEPS
    cd::core
    cd::material                   # AlphaMode enum
    cd::asset_material_authoring   # AuthoredMaterial
    cd::ui_renderer                # DrawBatcher
    cd::ui_widgets                 # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
