# engine/game/particles_event - cd::game::particles_event  (Phase 498 / G3.4)
#
# Recipe-driven gameplay particle dispatcher. Stages authored recipes and
# active bursts; does NOT simulate per-particle physics and does NOT link
# against cd::render::gpu_particles. The render-side adapter (one tier
# above this library) reads `active_bursts()` each frame and translates
# each burst into the actual GPU sink's spawn / advance commands.
#
# Dependencies (CLAUDE.md S7):
#   cd::core - CD_NODISCARD + base defines (PUBLIC, in header).
#   cd::math - Vec3f / Vec4f / Quatf (PUBLIC, in header).
#   cd::log  - CD_LOG_WARN on unknown-recipe fires (PRIVATE, .cpp only).

cd_add_library(game_particles_event
  TYPE STATIC
  SOURCES
    src/ParticlesEvent.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
  PRIVATE_DEPS
    cd::log
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
