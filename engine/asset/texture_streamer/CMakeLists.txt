# engine/asset/texture_streamer — cd::asset_texture_streamer (Phase 599 + 714)
#
# Sprint-1: synchronous GPU-side texture streaming coordinator.
# Sprint-2: opt-in async path via AsyncTexturePool (Phase 714).
#
# Sibling to cd::asset_scene_streamer (Phase 586).
# Depends on cd::rhi for IDevice + TextureHandle.
cd_add_library(asset_texture_streamer
  TYPE STATIC
  SOURCES
    src/TextureStreamer.cpp
    src/AsyncTexturePool.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset
    cd::rhi
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
