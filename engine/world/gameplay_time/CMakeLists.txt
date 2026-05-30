# engine/world/gameplay_time — cd::gameplay_time (Phase 459)
#
# Authoritative game-time abstraction with multi-channel TimerCategory
# (kGameplay / kUi / kSimulation). Header + single TU implementation;
# tiny enough to inline but kept out-of-line to amortize TU footprint
# across consumers and to keep <cd/gameplay/time/Time.hpp> dependency-free.
#
# Dependencies (CLAUDE.md §7): cd::core only — no math, no concurrency,
# no scene/ECS. Higher tiers consume this header to drive their own clocks.

cd_add_library(gameplay_time
  TYPE STATIC
  SOURCES
    src/Time.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
