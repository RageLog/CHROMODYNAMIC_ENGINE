# engine/ui/editor/panel_status_bar_fps_sparkline — cd::editor_panel_status_bar_fps_sparkline
#
# phase787 — FPS sparkline status-bar widget.
# Provides cd::editor::status_bar::FpsSparkline
# (DrawBatcher path; no ImGui).
#
# Ring buffer of 120 per-frame times.  draw() renders a bottom-anchored waveform
# (thin quads) colour-coded by average frame time:
#   avg < 16.67 ms → accent_success (green)
#   avg < 33.33 ms → accent_warning (amber)
#   avg >= 33.33 ms → accent_error  (red)
#
# Width = 200 px; fits in any status bar strip.
#
# MOMENT: FPS history at a glance — dev spots hitches without opening the profiler.
#
# Targets:
#   cd_editor_panel_status_bar_fps_sparkline   (real target)
#   cd::editor_panel_status_bar_fps_sparkline  (alias)

cd_add_library(editor_panel_status_bar_fps_sparkline
  TYPE STATIC
  SOURCES
    src/FpsSparkline.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer   # DrawBatcher + Color
    cd::ui_widgets    # Theme (widgets) + Rect
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
