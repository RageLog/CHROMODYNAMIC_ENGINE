# engine/game/cutscene_player - cd::game_cutscene_player (Phase 610 / M7 W4B)
#
# Data-driven cutscene timeline player. A Cutscene is a sequence of
# CutscenePhase entries, each with a duration and a list of CutsceneEvents
# to fire at specific offsets within that phase.
#
# Phase 703 / M15 W4B — JSON round-trip (save_to_json / load_from_json) added.
# Depends on cd::asset_json for serialization; cd::core for the player state machine.

cd_add_library(game_cutscene_player
  TYPE STATIC
  SOURCES
    src/CutscenePlayer.cpp
    src/CutsceneJson.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset_json
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
