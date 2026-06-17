# engine/asset/audio_streamer — cd::asset_audio_streamer (Phase 619 + 756)
#
# Sprint-1: synchronous audio clip streaming coordinator.
# Sprint-2: opt-in async path via AsyncAudioPool (Phase 756).
#
# Sibling to cd::asset_scene_streamer (Phase 586) and
# cd::asset_texture_streamer (Phase 599 + 714) — all three form the
# asset-streaming family.
#
# Depends on cd::asset for AssetId (clip identity) and cd::core for Defines.
# Band 6: real WAV PCM decode via cd::asset_wav (OGG sealed — no loader yet).
cd_add_library(asset_audio_streamer
  TYPE STATIC
  SOURCES
    src/AudioStreamer.cpp
    src/AsyncAudioPool.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset
  PRIVATE_DEPS
    cd::asset_wav
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
