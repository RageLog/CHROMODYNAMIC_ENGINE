# engine/world/net_session_replay — cd::net_session_replay (Phase 600)
#
# Record + replay of network packets for debugging multiplayer issues.
#
# Public types:
#   cd::net::session_replay::PacketRecord  — timestamped packet snapshot
#   cd::net::session_replay::Recorder      — capture + serialise to binary file
#   cd::net::session_replay::Replayer      — load + time-driven playback
#
# Binary file format: SRPK magic + version 1 header + per-packet entries.
# See SessionReplay.hpp for the full format spec.
#
# Dependencies (CLAUDE.md §7):
#   cd::core  — Defines.hpp (CD_NODISCARD, etc.)

cd_add_library(net_session_replay
  TYPE STATIC
  SOURCES
    src/SessionReplay.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
