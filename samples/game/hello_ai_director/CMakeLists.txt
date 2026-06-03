# samples/game/hello_ai_director — Phase 634 (M9 W2 closeout).
#
# Console-only smoke that exercises cd::game::ai_director end-to-end:
#   * Configure 3 EncounterTemplates (patrol_group / goblin_raid / boss_ambush)
#   * Simulate 20 ticks with interleaved player events
#   * Print TensionState transitions + request_next_encounter results
#   * Exit 0
#
# Dependencies (CLAUDE.md §7): cd::game_ai_director + cd::core.
cd_add_sample(hello_ai_director
  SOURCES main.cpp
  DEPS
    cd::core
    cd::game_ai_director
)
