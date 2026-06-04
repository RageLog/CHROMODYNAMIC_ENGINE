# engine/ui/editor/panel_perf_profiler — cd::editor_panel_perf_profiler
#
# phase700 — Deep frame-by-frame performance profiler panel (M15 W2C).
# Provides cd::editor::panel::perf_profiler::PerfProfiler (DrawBatcher path).
#
# Three-section layout:
#   A. 60-frame rolling bar chart (over/under-budget colour coding).
#   B. Drill-down: CPU markers + GPU pass records for the selected frame.
#   C. Stats strip: avg / min / max / p99 total_ms over the history window.
#
# MOMENT: a perf-sensitive dev captures 60 frames, sees a spike at frame 23,
# drills into it, finds the 'TLAS rebuild' GPU pass at 12 ms — root cause
# spotted in seconds without leaving the editor.
#
# Targets:
#   cd_editor_panel_perf_profiler   (real target)
#   cd::editor_panel_perf_profiler  (alias)

cd_add_library(editor_panel_perf_profiler
  TYPE STATIC
  SOURCES
    src/PerfProfiler.cpp
  PUBLIC_DEPS
    cd::core
    cd::ui_renderer                    # DrawBatcher (draw() parameter)
    cd::ui_theme                       # cd::ui::theme::Theme
    cd::ui_widgets                     # Rect
    cd::profile_cpu_marker_overlay     # MarkerSample (FrameSnapshot member)
    cd::profile_gpu_marker             # GpuMarkerSample (FrameSnapshot member)
    cd::profile_frame_graph_timeline   # PassRecord (FrameSnapshot member)
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
