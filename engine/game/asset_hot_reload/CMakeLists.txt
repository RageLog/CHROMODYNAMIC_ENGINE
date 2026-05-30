# engine/game/asset_hot_reload — cd::game_asset_hot_reload (Phase 502, G5.1
# from ADR-20260530-gameplay-library-family).
#
# Runtime asset reload dispatcher. Polls cd::asset::FileWatcher (the production
# producer, header-only via cd::asset INTERFACE library), routes events into
# typed subscribers grouped by `AssetCategory`, and throttles rapid bursts to
# one callback per file per 100 ms (default). The library sits at the bottom
# of the gameplay reload pipeline — the GPU / audio / locale / shader sinks
# live one tier above and pull state from this bus during their own ticks.
#
# Dependencies (CLAUDE.md §7):
#   * cd::core   — Defines + general utilities (no Result here yet because
#                  the surface is success-or-no-op; future async variants
#                  may grow Result<T> returns).
#   * cd::asset  — header-only INTERFACE library that publishes
#                  `cd::asset::FileWatcher`. PRIVATE_DEPS — the public API
#                  forward-declares FileWatcher, so callers do not transitively
#                  pull cd::asset.

cd_add_library(game_asset_hot_reload
  TYPE STATIC
  SOURCES
    src/HotReload.cpp
  PUBLIC_DEPS
    cd::core
  PRIVATE_DEPS
    cd::asset
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
