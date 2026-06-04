cd_add_test(editor_panel_debug_viz
  SOURCES test_debug_viz.cpp
  DEPS
    cd::editor_panel_debug_viz
    cd::ui_renderer
    cd::ui_widgets
)

# phase735 — Sprint-2 real G-buffer texture overlay coverage.
cd_add_test(editor_panel_debug_viz_real_textures
  SOURCES test_debug_viz_real_textures.cpp
  DEPS
    cd::editor_panel_debug_viz
    cd::rhi
    cd::ui_renderer
    cd::ui_widgets
)
