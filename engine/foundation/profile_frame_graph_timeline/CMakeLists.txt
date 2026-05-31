# engine/foundation/profile_frame_graph_timeline — cd::profile_frame_graph_timeline
#
# Phase 593 — GPU-side Gantt-chart timeline overlay.
#
# Provides Timeline (per-frame GPU pass accumulator + double-buffer) and
# TimelineOverlay (horizontal Gantt chart into cd::ui::renderer::DrawBatcher).
#
# Companion library to cd::profile_cpu_marker_overlay (Phase 587).
#
# Note: cd::ui_renderer is a PRIVATE dependency — DrawBatcher appears only
# in TimelineOverlay::draw() which lives in the .cpp. Consumers of this
# library need only include FrameGraphTimeline.hpp; they do not transitively
# pull in the full ui renderer headers.

cd_add_library(profile_frame_graph_timeline
  TYPE STATIC
  SOURCES
    src/FrameGraphTimeline.cpp
  PUBLIC_DEPS
    cd::core
  PRIVATE_DEPS
    cd::ui_renderer
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
