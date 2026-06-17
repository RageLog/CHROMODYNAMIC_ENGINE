# engine/asset/texture_streamer — cd::asset_texture_streamer (Phase 599 + 714)
#
# Synchronous + opt-in async GPU-side texture streaming coordinator.
# Band 6: real .cdtex BC7 decode wired via cd::asset_cdtex.
#
# NOTE: we deliberately do NOT depend on cd::asset_image here. Both
# cd::asset_image and cd::asset_gltf vendor their own STB_IMAGE_IMPLEMENTATION;
# pulling cd::asset_image into this static lib would collide with the
# cd::asset_gltf copy when streamer_pool links texture + scene streamers in one
# executable (lld duplicate-symbol). The engine's cooked texture format is
# .cdtex, which is the named Band-6 gap — see ADR-20260616-band6 for the
# PNG/JPG-via-image-loader seal + trigger.
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
  PRIVATE_DEPS
    cd::asset_cdtex
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
