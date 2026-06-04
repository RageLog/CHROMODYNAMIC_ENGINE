# engine/world/net_lobby — cd::network::lobby (Phase 713 / Sprint W5A)
#
# Sprint-1: in-memory room-state management for multiplayer sessions.
# Network sync (WebSocket / reliable-UDP broadcast) is Sprint-2.
#
# Public types:
#   cd::network::lobby::LobbyConfig   — room config (name, mode, capacity, passcode)
#   cd::network::lobby::PlayerState   — per-player snapshot (id, display_name, ready, team)
#   cd::network::lobby::RoomState     — aggregate config + players + lifecycle flags
#   cd::network::lobby::Lobby         — create / join / leave / ready / start registry
#
# Sibling library:
#   cd::net::matchmaker (Phase 563) — pairs players into session slots.
#   cd::network::lobby  (this)      — manages the room once players are paired.
#
# Dependencies (CLAUDE.md §7):
#   cd::core  — Defines.hpp (CD_NODISCARD, etc.)
#   cd::net   — Sprint-2 transport integration; linked now so consumers
#               that wire real backends need no link-line changes.
#               No header from cd::net is included by Sprint-1 code.

cd_add_library(net_lobby
  TYPE STATIC
  SOURCES
    src/Lobby.cpp
    src/Packet.cpp
    src/Transport.cpp
  PUBLIC_DEPS
    cd::core
    cd::net
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
