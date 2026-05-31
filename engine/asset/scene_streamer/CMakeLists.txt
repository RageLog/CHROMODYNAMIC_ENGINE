# engine/asset/scene_streamer — cd::asset_scene_streamer (Phase 586)
#
# Sprint-1: synchronous scene streaming coordinator.
# Sprint-2: async job submission (future).
#
# Depends on cd::asset_gltf for load_scene() + LoadedScene.
# Depends on cd::scene for future ECS ingest integration.
cd_add_library(asset_scene_streamer
  TYPE STATIC
  SOURCES
    src/SceneStreamer.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset
    cd::asset_gltf
    cd::scene
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
