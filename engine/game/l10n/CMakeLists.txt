# engine/game/l10n - cd::game_l10n (Phase 501 / G4.3)
#
# Localization runtime: line-based .lang loader, CLDR plural rules (en/tr
# baked in, extensible), RTL helper, locale fallback chain + observer hooks.
#
# Dependencies (CLAUDE.md S7): cd::core only. Pure data + std::ifstream;
# no allocator, no math, no I/O beyond a flat key=value loader.

cd_add_library(game_l10n
  TYPE STATIC
  SOURCES
    src/L10n.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
