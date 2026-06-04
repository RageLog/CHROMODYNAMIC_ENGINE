# engine/asset/vfx_authoring — cd::asset_vfx_authoring (Phase 694)
#
# Asset-tier VFX authoring helper: hand-edit .vfx.json, validate before the
# effect description enters cd::particle::system. JSON round-trip + 4 presets
# (jump_dust / muzzle_flash / fire_smoke / water_splash).
#
# Moment: a VFX artist authors a 'jump_dust' effect as JSON, hot-reload picks
# it up, and the engine spawns the new particles immediately on the next jump
# — zero engineer round-trips.
#
# Depends on cd::asset_json (JSON I/O) and cd::core.
# Does NOT depend on cd::rhi — stays in the asset tier.
cd_add_library(asset_vfx_authoring
  TYPE STATIC
  SOURCES
    src/VfxAuthoring.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset_json
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
