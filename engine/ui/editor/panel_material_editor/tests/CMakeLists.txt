cd_add_test(editor_panel_material_editor
  SOURCES test_material_editor.cpp
  DEPS
    cd::editor_panel_material_editor
    cd::ui_renderer
    cd::ui_widgets
)
