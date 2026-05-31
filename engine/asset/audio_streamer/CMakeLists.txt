# engine/asset/audio_streamer — cd::asset_audio_streamer (Phase 619)
#
# Sprint-1: synchronous audio clip streaming coordinator.
# Sprint-2: async job submission + real WAV/OGG decode (future).
#
# Sibling to cd::asset_scene_streamer (Phase 586) and
# cd::asset_texture_streamer (Phase 599) — all three form the asset-streaming
# family.
#
# Depends on cd::asset for AssetId (clip identity) and cd::core for Defines.
cd_add_library(asset_audio_streamer
  TYPE STATIC
  SOURCES
    src/AudioStreamer.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
