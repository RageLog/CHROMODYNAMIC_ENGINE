# engine/ui/editor/panel_console — cd::editor_panel_console
#
# phase544 — Console panel library.
# Provides cd::editor::panel::console::Console (DrawBatcher path).
#
# Targets:
#   cd_editor_panel_console   (real target)
#   cd::editor_panel_console  (alias)

cd_add_library(editor_panel_console
  TYPE STATIC
  SOURCES
    src/Console.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
