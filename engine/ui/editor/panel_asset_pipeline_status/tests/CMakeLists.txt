cd_add_test(editor_panel_asset_pipeline_status
  SOURCES test_asset_pipeline_status.cpp
  DEPS
    cd::editor_panel_asset_pipeline_status
    cd::asset_streamer_pool
    cd::ui_renderer
    cd::ui_widgets
)
