# engine/game/save — cd::game_save (Phase 470, G1.3 from
# ADR-20260530-gameplay-library-family).
#
# Slot-based persistence with atomic on-disk writes. The library is
# deliberately cd::core-only at every level (no zstd / no rapidjson) so it
# stays at the bottom of the cd::game::* DAG and can host both world
# snapshots (eventual Phase G5) and tiny per-feature persistence
# (settings backup, telemetry sidecar, replay header) without forcing a
# heavy transitive include set on consumers.
#
# Atomic write contract: every body and meta blob is first written to a
# `.tmp` sibling, then renamed over the final path. On crash the previous
# body remains intact (or, if first write, no body at all).

cd_add_library(game_save
  TYPE STATIC
  SOURCES
    src/Save.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
