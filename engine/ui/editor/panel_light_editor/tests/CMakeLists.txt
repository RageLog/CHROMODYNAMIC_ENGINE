cd_add_test(editor_panel_light_editor
  SOURCES test_light_editor.cpp
  DEPS
    cd::editor_panel_light_editor
    cd::lighting_clusters
    cd::ui_renderer
    cd::ui_widgets
)
