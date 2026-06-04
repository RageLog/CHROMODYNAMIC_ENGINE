cd_add_test(editor_panel_pathfinding_viz
  SOURCES test_pathfinding_viz.cpp
  DEPS
    cd::editor_panel_pathfinding_viz
    cd::game_ai_pathfinding
    cd::ui_renderer
    cd::ui_widgets
)
