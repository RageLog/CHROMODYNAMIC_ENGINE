cd_add_test(editor_panel_viewport
  SOURCES test_viewport.cpp
  DEPS
    cd::editor_panel_viewport
    cd::ui_renderer
    cd::ui_widgets
    cd::rhi
)
