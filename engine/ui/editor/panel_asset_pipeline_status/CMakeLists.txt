# engine/ui/editor/panel_asset_pipeline_status — cd::editor_panel_asset_pipeline_status
#
# phase709 — Asset Pipeline Status panel (DrawBatcher path; 4 horizontal bar
# charts: scene / texture / audio / shader — pending vs. completed counts).
# Drives cd::asset::streamer_pool::StreamerPool (M15 phase705).
#
# MOMENT: dev watches the pipeline panel during scene-load, spots the texture
# streamer at pending=200 vs scene=50, tunes priorities before the next
# PlayTest without touching the terminal.
#
# Targets:
#   cd_editor_panel_asset_pipeline_status   (real target)
#   cd::editor_panel_asset_pipeline_status  (alias)

cd_add_library(editor_panel_asset_pipeline_status
  TYPE STATIC
  SOURCES
    src/AssetPipelineStatus.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset_streamer_pool    # StreamerPool + PoolStats
    cd::ui_renderer             # DrawBatcher (draw() parameter)
    cd::ui_widgets              # Theme + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
