# engine/world/particle_system - cd::particle::system  (Phase 564 / M3 W5C; renamed Phase 583)
#
# CPU-side particle simulation system.
# Note: name corrected from particle_system_v2 in M5 — no v1 ever existed, suffix was misleading.
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

cd_add_library(particle_system
  TYPE STATIC
  SOURCES
    src/ParticleSystemV2.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
