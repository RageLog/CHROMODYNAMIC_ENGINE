# engine/ui/editor/panel_asset_drop_target — cd::editor_panel_asset_drop_target
#
# phase594 — Asset Drop Target panel library.
# Provides cd::editor::panel::asset_drop_target::AssetDropTarget (DrawBatcher path).
# Drop-zone widget that:
#   * Renders a dashed-border rectangle labelled "Drop assets here".
#   * Filters incoming paths by accepted file extensions.
#   * Surfaces dropped paths to the caller via consume_dropped_path().
#   * Exposes simulate_drop() for headless unit tests.
#
# Targets:
#   cd_editor_panel_asset_drop_target   (real target)
#   cd::editor_panel_asset_drop_target  (alias)

cd_add_library(editor_panel_asset_drop_target
  TYPE STATIC
  SOURCES
    src/AssetDropTarget.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer     # DrawBatcher
    cd::ui_widgets      # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
