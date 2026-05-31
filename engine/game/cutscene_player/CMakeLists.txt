# engine/game/cutscene_player - cd::game_cutscene_player (Phase 610 / M7 W4B)
#
# Data-driven cutscene timeline player. A Cutscene is a sequence of
# CutscenePhase entries, each with a duration and a list of CutsceneEvents
# to fire at specific offsets within that phase.
#
# Dependencies (CLAUDE.md §7): cd::core only. No allocator, no math, no I/O.
# The player is a pure state machine; the engine routes fired events to the
# appropriate backend (audio, render, ECS) from the outside.

cd_add_library(game_cutscene_player
  TYPE STATIC
  SOURCES
    src/CutscenePlayer.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
