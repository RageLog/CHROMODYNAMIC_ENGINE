cd_add_test(editor_panel_material_preview
  SOURCES test_material_preview.cpp
  DEPS
    cd::editor_panel_material_preview
    cd::asset_material_authoring
    cd::ui_renderer
    cd::ui_widgets
)
