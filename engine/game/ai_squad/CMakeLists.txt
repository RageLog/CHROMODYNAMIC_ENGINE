# engine/game/ai_squad — cd::ai::squad (Phase 682 / M13 W5A)
#
# Multi-agent squad coordination layer: Left 4 Dead Special Infected /
# Halo Flood-style squad logic. Sprint-1 ships:
#   * SquadRole enum (kPointman / kFlanker / kSuppressor / kCoverer)
#   * Formation snapshot (centroid + radius + member_count)
#   * Blackboard shared knowledge store (target + threat_level + facts map)
#   * Squad class: add/remove/update/tick/query API
#
# Tick computes formation centroid, radius, and exponential threat decay.
# Sprint-2 plan: MoveCommand integration (steering), communication graph
# (line-of-sight between members), and cover-point reservation.
#
# Design references:
#   * Booth, M. "The AI Systems of Left 4 Dead." AIIDE 2009.
#   * Isla, D. "Handling Complexity in the Halo 2 AI." GDC 2005.
#   * van der Sterren, W. "Squad Tactics for Video Games." AI Game
#     Programming Wisdom 3, 2006.
#
# Dependencies (CLAUDE.md §7): cd::core only — pure stdlib, no ECS/render/audio.
#
# Moment: A combat designer creates a 4-agent enemy squad, sees them advance
# in formation with role-specific behaviors (pointman scouts, flankers go wide,
# suppressor lays cover) without writing each agent's logic from scratch.

cd_add_library(game_ai_squad
  TYPE STATIC
  SOURCES
    src/AiSquad.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
