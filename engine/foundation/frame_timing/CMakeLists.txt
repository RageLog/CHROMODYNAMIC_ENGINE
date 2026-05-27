# engine/foundation/frame_timing — cd::frame_timing
#
# Header-only ring buffer of recent dt samples + min/mean/median/p99
# accessors. Use anywhere a per-frame "FPS HUD" or stutter detector
# needs to summarise the last N frames without pulling in cd::profile.

cd_add_library(frame_timing
  TYPE INTERFACE
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
