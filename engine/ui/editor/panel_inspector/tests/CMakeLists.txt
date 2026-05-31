cd_add_test(editor_panel_inspector
  SOURCES test_inspector.cpp
  DEPS
    cd::editor_panel_inspector
    cd::scene
    cd::ecs
    cd::ui_renderer
    cd::ui_widgets
)
