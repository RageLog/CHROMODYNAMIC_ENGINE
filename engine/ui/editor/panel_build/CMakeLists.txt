# engine/ui/editor/panel_build — cd::editor_panel_build
#
# phase699 — Build Panel library.
# Provides cd::editor::panel::build::BuildPanel (DrawBatcher path).
#
# Top half: colour-coded Status badge (idle/compiling/success/failed).
# Bottom half: scrollable BuildEvent log with timestamp + message,
# colour-coded by severity. Click on an event with a source_file emits
# selected_event_index (Sprint-2 = open file in code editor at that line).
#
# MOMENT: A dev presses Build, sees compile progress live in the panel —
# green check when done, red errors with file+line on failure.
# No external terminal needed.
#
# Targets:
#   cd_editor_panel_build   (real target)
#   cd::editor_panel_build  (alias)

cd_add_library(editor_panel_build
  TYPE STATIC
  SOURCES
    src/BuildPanel.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer   # DrawBatcher
    cd::ui_widgets    # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
