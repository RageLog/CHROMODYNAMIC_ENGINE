cd_add_test(editor_panel_auto_save_indicator
  SOURCES test_auto_save_indicator.cpp
  DEPS
    cd::editor_panel_auto_save_indicator
    cd::ui_renderer
    cd::ui_theme
    cd::ui_widgets
)
