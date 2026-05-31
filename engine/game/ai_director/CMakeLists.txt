# engine/game/ai_director — cd::game::ai_director (Phase 620 / M8 W3B)
#
# High-level AI pacing controller in the Left 4 Dead / Skyrim Radiant Quest
# tradition.  Manages a continuous intensity curve over TensionState buckets
# (Idle → BuildUp → Peak → Relief → Idle) and schedules EncounterTemplate
# spawns keyed to the current pacing level.
#
# Design references:
#   * Booth, M. "The AI Systems of Left 4 Dead." AIIDE 2009.
#   * Bethesda. "Radiant Quest System." GDC 2012.
#   * Yannakakis & Togelius. "Artificial Intelligence and Games." Springer 2018.
#
# Dependencies (CLAUDE.md §7): cd::core only — the Director is a pure state
# machine; ECS / audio / render sinks are wired by the caller at the game
# tier, never pulled in here.

cd_add_library(game_ai_director
  TYPE STATIC
  SOURCES
    src/AiDirector.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
