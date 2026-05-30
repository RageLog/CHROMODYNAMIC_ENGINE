# engine/game/dialogue - cd::game_dialogue (Phase 484 / G4.1)
#
# Branching dialogue VM. Sits in the cd::game::* tier above world/script and
# below editor/samples.
#
# Dependencies (CLAUDE.md S7): cd::core only. The library is pure data + a
# small VM -- no allocator, no math, no I/O beyond the in-memory DSL parser.

cd_add_library(game_dialogue
  TYPE STATIC
  SOURCES
    src/Dialogue.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
