cd_add_test(editor_panel_asset_drop_target
  SOURCES test_asset_drop_target.cpp
  DEPS
    cd::editor_panel_asset_drop_target
    cd::ui_renderer
    cd::ui_widgets
)
