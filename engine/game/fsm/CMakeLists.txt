# engine/game/fsm - cd::game_fsm  (Phase 472 / G2.1)
#
# Hierarchical Finite State Machine with regions + history (shallow/deep).
# Design references: Harel 1987 "Statecharts" + UML 2.5.1 state diagrams.
#
# Dependencies (CLAUDE.md S7): cd::core only.
# The library is header-only template code; the .cpp is a TU anchor so this
# stays a normal STATIC target consistent with the rest of the engine.

cd_add_library(game_fsm
  TYPE STATIC
  SOURCES
    src/Fsm.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
