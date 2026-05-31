# engine/foundation/profile_cpu_marker_overlay — cd::profile_cpu_marker_overlay
#
# Phase 587 — Tracy-style CPU marker bar-chart overlay.
#
# Provides Collector (ring buffer of MarkerSamples), RAII Scope, and
# Overlay (emits coloured quads into cd::ui::renderer::DrawBatcher).
#
# Note: cd::ui_renderer is a PRIVATE dependency — the DrawBatcher type
# appears only in Overlay::draw() which lives in the .cpp. Consumers of
# this library need only include CpuMarkerOverlay.hpp; they do not
# transitively pull in the full ui renderer headers.

cd_add_library(profile_cpu_marker_overlay
  TYPE STATIC
  SOURCES
    src/CpuMarkerOverlay.cpp
  PUBLIC_DEPS
    cd::core
  PRIVATE_DEPS
    cd::ui_renderer
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
