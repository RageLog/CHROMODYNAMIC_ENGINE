cd_add_test(editor_panel_build
  SOURCES test_build_panel.cpp
  DEPS
    cd::editor_panel_build
    cd::ui_renderer
    cd::ui_widgets
)
