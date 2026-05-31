# engine/ui/editor/panel_behavior_designer — cd::editor_panel_behavior_designer
#
# phase556-557-558 — Behavior Designer panel library.
# Provides cd::editor::panel::behavior_designer::BehaviorDesigner (DrawBatcher path).
# Behavior tree graph view:
#   * Placeholder grid + 3 demo nodes + connecting edge lines (Sprint-1).
#   * Click-to-select node (set_selected), drag-to-pan view (set_pan_offset).
#   * Bind to a BT root node via set_tree_root(BehaviorNodeId).
#   * Sprint-2 will add full BT graph data from cd::ai/cd::behavior subsystem.
#
# Targets:
#   cd_editor_panel_behavior_designer   (real target)
#   cd::editor_panel_behavior_designer  (alias)

cd_add_library(editor_panel_behavior_designer
  TYPE STATIC
  SOURCES
    src/BehaviorDesigner.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
