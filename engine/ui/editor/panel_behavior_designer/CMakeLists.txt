# engine/ui/editor/panel_behavior_designer — cd::editor_panel_behavior_designer
#
# phase556-557-558 — Sprint-1: placeholder grid + 3 demo nodes.
# phase740         — Sprint-2: real BT graph from cd::game::ai_bt::BehaviorTree.
#   * set_tree(const BehaviorTree*) binds a live BT.
#   * Recursive top-down auto-layout (measure_subtree_).
#   * NodeKind color coding (Selector/Sequence/Parallel/Decorator/Leaf).
#   * Orthogonal edges parent→children.
#   * Click-selects via set_selected(BehaviorNodeId).
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
    cd::game_ai_bt      # BehaviorTree, Node hierarchy, NodeKind, node_kind()
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
