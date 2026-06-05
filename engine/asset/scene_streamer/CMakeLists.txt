# engine/asset/scene_streamer — cd::asset_scene_streamer (Phase 586 + 755)
#
# Sprint-1: synchronous scene streaming coordinator.
# Sprint-2: opt-in async path via AsyncScenePool (Phase 755).
#
# Depends on cd::asset_gltf for load_scene() + LoadedScene.
# Depends on cd::scene for future ECS ingest integration.
cd_add_library(asset_scene_streamer
  TYPE STATIC
  SOURCES
    src/SceneStreamer.cpp
    src/AsyncScenePool.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset
    cd::asset_gltf
    cd::scene
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
