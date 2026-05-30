# engine/game/trigger - cd::game_trigger  (Phase 476 / G3.2)
#
# Trigger volumes + on_enter / on_stay / on_exit dispatch.  Sits in the
# cd::game::* tier (above world/physics primitives, below editor/samples).
#
# Dependencies (CLAUDE.md S7):
#   cd::core    - CD_NODISCARD + base defines.
#   cd::math    - Vec3f for subject positions.
#   cd::ecs     - Entity opaque handle (owner + subject keys).
#   cd::physics - Aabb + Sphere primitives + contains() inlines.
#
# cd::game::query (G3.1) was the brief's intended broad-phase index but had
# not been committed at the time of this wave; the tick() signature already
# accepts a `QueryWorld*` (opaque forward-declared) so the indexed path can
# be plumbed in without a header-level API change.  See ADR note inside
# Trigger.hpp for the integration plan.

cd_add_library(game_trigger
  TYPE STATIC
  SOURCES
    src/Trigger.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::ecs
    cd::physics
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
