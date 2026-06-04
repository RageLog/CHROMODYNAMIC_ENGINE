cd_add_test(editor_panel_cutscene_player
  SOURCES test_cutscene_player_panel.cpp
  DEPS
    cd::editor_panel_cutscene_player
    cd::game_cutscene_player
    cd::ui_renderer
    cd::ui_widgets
)

# phase741 — Pan/zoom Sprint-2 tests.
cd_add_test(cd_test_cutscene_pan_zoom
  SOURCES test_cutscene_pan_zoom.cpp
  DEPS
    cd::editor_panel_cutscene_player
    cd::game_cutscene_player
    cd::ui_renderer
    cd::ui_widgets
)
