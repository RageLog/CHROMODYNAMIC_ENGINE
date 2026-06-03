cd_add_test(scene_ingest
  SOURCES test_scene_ingest.cpp
  DEPS    cd::render_scene_ingest cd::render cd::rhi cd::ecs cd::scene
          cd::asset_gltf cd::math cd::core
)

# M11 W1 T1.15 — RenderBucket / partition_prims (curtain alpha-order fix).
cd_add_test(render_bucket_split
  SOURCES test_render_bucket_split.cpp
  DEPS    cd::render_scene_ingest cd::material cd::core
)
