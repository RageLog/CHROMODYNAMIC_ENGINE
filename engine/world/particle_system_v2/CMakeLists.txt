# engine/world/particle_system_v2 - cd::particle::system_v2  (Phase 564 / M3 W5C)
#
# CPU-side particle simulation system v2.
#
# Provides:
#   * ParticleSpec + EmitterSpec — authored parameters.
#   * System                    — tick(dt) + add/remove emitter + snapshot.
#   * ParticleSnapshot          — renderer-facing read-only particle view.
#
# Design:
#   * SoA storage of parallel float vectors with swap-and-pop deletion.
#   * Semi-implicit Euler integration (velocity first, position second).
#   * Spawn accumulator: fractional particles carry over between ticks.
#   * Velocity sampling via std::mt19937 (no cd::math dependency).
#
# GPU dispatch is V3 — out of scope here.
#
# Dependencies (CLAUDE.md §7):
#   cd::core - CD_NODISCARD + base defines (PUBLIC, in header).

cd_add_library(particle_system_v2
  TYPE STATIC
  SOURCES
    src/ParticleSystemV2.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
