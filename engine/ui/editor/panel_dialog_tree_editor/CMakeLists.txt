# engine/ui/editor/panel_dialog_tree_editor — cd::editor_panel_dialog_tree_editor
#
# phase666 — Dialog Tree Editor panel library.
# Provides cd::editor::panel::dialog_tree_editor::DialogTreeEditor
# (DrawBatcher path).
# Renders a cd::game::dialog_tree::DialogTree as a visual graph:
#   * Colour-coded node rectangles per NodeKind
#     (kSay=blue, kChoice=yellow, kCondition=orange, kEnd=grey).
#   * L-shaped edge connectors drawn parent -> child for each next_id.
#   * Static left-to-right depth-column layout (Sprint-1).
#   * Click-to-select via simulate_click; accent border on selected node.
#   * Sprint-2: pan/zoom + live hot-reload binding.
#
# Targets:
#   cd_editor_panel_dialog_tree_editor   (real target)
#   cd::editor_panel_dialog_tree_editor  (alias)

cd_add_library(editor_panel_dialog_tree_editor
  TYPE STATIC
  SOURCES
    src/DialogTreeEditor.cpp
  PUBLIC_DEPS
    cd::core
    cd::game_dialog_tree    # DialogTree / DialogNode / NodeKind
    cd::ui_renderer          # DrawBatcher
    cd::ui_widgets           # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
