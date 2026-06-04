cd_add_test(editor_panel_input_recorder
  SOURCES test_input_recorder_panel.cpp
  DEPS
    cd::editor_panel_input_recorder
    cd::game_input_recorder
    cd::ui_renderer
    cd::ui_widgets
)
