cd_add_test(scene_ingest
  SOURCES test_scene_ingest.cpp
  DEPS    cd::render_scene_ingest cd::render cd::rhi cd::ecs cd::scene
          cd::asset_gltf cd::math cd::core
)
