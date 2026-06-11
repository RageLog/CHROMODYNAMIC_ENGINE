# engine/render/debug_draw — cd::debug_draw (phase1057)
# GPU half of the debug-line stack: kLineList pipeline + lazily-grown
# host-visible VB with frames-in-flight-safe (parked) growth + the
# once-per-frame flush. CPU batching lives in cd::debug_line.
cd_add_library(debug_draw
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # transitive on cd::shader (vendored glslang)
  SOURCES
    src/DebugDraw.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::debug_line
    cd::material
    cd::rhi
    cd::shader
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
