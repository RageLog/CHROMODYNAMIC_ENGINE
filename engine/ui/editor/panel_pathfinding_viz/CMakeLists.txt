# engine/ui/editor/panel_pathfinding_viz — cd::editor_panel_pathfinding_viz
#
# phase677 — PathfindingViz editor panel library.
# Provides cd::editor::panel::pathfinding_viz::PathfindingViz (DrawBatcher path).
# Top-down 2D navmesh projection + A* path overlay:
#   * Each NavMesh triangle rendered as a top-down-projected quad with outline.
#   * Last PathResult waypoints drawn as a thicker accent-coloured L-path.
#   * Stats strip: triangle count, path success/fail, distance, explored.
#   * simulate_click_to_test_path for interactive goal placement (Sprint-2: live query).
#   * Sprint-2: pan/zoom + live Pathfinder re-query on drag.
#
# Targets:
#   cd_editor_panel_pathfinding_viz   (real target)
#   cd::editor_panel_pathfinding_viz  (alias)

cd_add_library(editor_panel_pathfinding_viz
  TYPE STATIC
  SOURCES
    src/PathfindingViz.cpp
  PUBLIC_DEPS
    cd::core
    cd::game_ai_pathfinding   # NavMesh + PathResult + Pathfinder
    cd::ui_renderer            # DrawBatcher
    cd::ui_widgets             # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
