cd_add_test(editor_panel_perf_profiler
  SOURCES test_perf_profiler.cpp
  DEPS
    cd::editor_panel_perf_profiler
    cd::ui_renderer
    cd::ui_theme
    cd::ui_widgets
)
