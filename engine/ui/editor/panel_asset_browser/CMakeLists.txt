# engine/ui/editor/panel_asset_browser — cd::editor_panel_asset_browser
#
# phase545 — Asset Browser panel library.
# Provides cd::editor::panel::asset_browser::AssetBrowser (DrawBatcher path).
#
# Targets:
#   cd_editor_panel_asset_browser   (real target)
#   cd::editor_panel_asset_browser  (alias)

cd_add_library(editor_panel_asset_browser
  TYPE STATIC
  SOURCES
    src/AssetBrowser.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
