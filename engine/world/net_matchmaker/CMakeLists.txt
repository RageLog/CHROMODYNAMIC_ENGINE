# engine/world/net_matchmaker — cd::net_matchmaker (Phase 563 / Sprint W5B)
#
# Sprint-1: in-memory matchmaking data structures + skill/region matching.
# Network transport (real session service backend) is Sprint-2.
#
# Public types:
#   cd::net::matchmaker::PlayerProfile  — player id + skill_rating + region tag
#   cd::net::matchmaker::Lobby          — lobby_id + player_ids + game_mode
#   cd::net::matchmaker::LobbyRegistry  — create / join / leave / close + profile store
#   cd::net::matchmaker::SkillBasedFinder — skill + region aware lobby finder
#
# Dependencies (CLAUDE.md §7):
#   cd::core   — Defines.hpp (CD_NODISCARD, CD_CACHE_ALIGN, etc.)
#   cd::net    — Sprint-2 transport integration; listed as dependency so
#                consumers that wire the real backend do not need to change their
#                link line.  No header from cd::net is included by Sprint-1 code.

cd_add_library(net_matchmaker
  TYPE STATIC
  SOURCES
    src/Matchmaker.cpp
  PUBLIC_DEPS
    cd::core
    cd::net
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
