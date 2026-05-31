# engine/ui/editor/panel_material_editor — cd::editor_panel_material_editor
#
# phase556-557-558 — Material Editor panel library.
# Provides cd::editor::panel::material_editor::MaterialEditor (DrawBatcher path).
# Material parameter editor: dropdown visualisation for base color / metallic /
# roughness; live preview rectangle showing the current material's colour tint;
# bind to a MaterialId via set_material_id(MaterialId).
#
# Targets:
#   cd_editor_panel_material_editor   (real target)
#   cd::editor_panel_material_editor  (alias)

cd_add_library(editor_panel_material_editor
  TYPE STATIC
  SOURCES
    src/MaterialEditor.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
