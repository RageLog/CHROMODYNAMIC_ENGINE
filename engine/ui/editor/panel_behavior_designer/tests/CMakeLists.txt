cd_add_test(editor_panel_behavior_designer
  SOURCES test_behavior_designer.cpp
  DEPS
    cd::editor_panel_behavior_designer
    cd::game_ai_bt
    cd::ui_renderer
    cd::ui_widgets
)
