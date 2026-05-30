# engine/game/quest - cd::game_quest (Phase 500 / G4.2)
#
# Objective tracker + journal. Sits in the cd::game::* tier above world/script
# and below editor/samples.
#
# Dependencies (CLAUDE.md S7): cd::core only. The library is pure data + a
# small VM -- no allocator, no math, no I/O beyond the in-memory byte-blob
# serialiser. The serialize/restore pair returns raw bytes so callers can
# route them through cd::game::save (or any other persistence) without
# forcing a transitive link-time dependency here.

cd_add_library(game_quest
  TYPE STATIC
  SOURCES
    src/Quest.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
