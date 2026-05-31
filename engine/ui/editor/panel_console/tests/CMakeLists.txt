cd_add_test(editor_panel_console
  SOURCES test_console.cpp
  DEPS
    cd::editor_panel_console
    cd::ui_renderer
    cd::ui_widgets
)
