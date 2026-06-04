# engine/ui/editor/panel_settings — cd::editor_panel_settings
#
# phase698 — Settings panel library.
# Provides cd::editor::panel::settings::SettingsPanel (DrawBatcher path).
#
# 5-category tab strip (Graphics / Input / Audio / Editor / Advanced) +
# key/value entry registry. User edits vsync=on → vsync=off; engine reads
# the change the next frame.
#
# MOMENT: A user opens Settings, switches to Graphics tab, edits vsync to off
# — the engine respects it the next frame.
#
# Targets:
#   cd_editor_panel_settings   (real target)
#   cd::editor_panel_settings  (alias)

cd_add_library(editor_panel_settings
  TYPE STATIC
  SOURCES
    src/SettingsPanel.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer   # DrawBatcher
    cd::ui_widgets    # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
