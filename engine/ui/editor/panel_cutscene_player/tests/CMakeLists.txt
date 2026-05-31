cd_add_test(editor_panel_cutscene_player
  SOURCES test_cutscene_player_panel.cpp
  DEPS
    cd::editor_panel_cutscene_player
    cd::game_cutscene_player
    cd::ui_renderer
    cd::ui_widgets
)
