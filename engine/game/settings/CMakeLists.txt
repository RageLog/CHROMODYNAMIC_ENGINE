# engine/game/settings — cd::game::settings (Phase 471, ADR G1.4)
#
# Persistent user-preferences library. Strongly-typed key/value store with
# a small typed variant (bool / int / float / string), simple INI-style
# on-disk format (key=value lines, '#' comments preserved on save), and
# a live observer model (subscribers fire only on actual value change).
#
# Dependencies (CLAUDE.md §7): cd::core only. The library is intentionally
# infrastructure-grade — no rendering, no platform, no asset coupling — so
# settings can be loaded from a tiny POD on startup before any subsystem
# initialises (graphics resolution decided before RHI device creation,
# language decided before l10n, audio backend choice before mixer boot).

cd_add_library(game_settings
  TYPE STATIC
  SOURCES
    src/Settings.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
