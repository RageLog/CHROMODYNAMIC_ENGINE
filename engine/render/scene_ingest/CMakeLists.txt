# engine/render/scene_ingest — Layer 2 of generic glTF load (ADR-20260530)
#
# cd::render_scene_ingest — takes a cd::asset::gltf::LoadedScene and lands it
# on the GPU + into the ECS / Scene tree via ingest_gltf_scene().
#
# Sits between cd::asset_gltf (LoadedScene producer) and cd::render (mesh /
# texture upload helpers); depends on cd::ecs + cd::scene for entity create.

cd_add_library(render_scene_ingest
  TYPE STATIC
  SOURCES
    src/SceneIngest.cpp
  PUBLIC_DEPS
    cd::core
    cd::math
    cd::rhi
    cd::ecs
    cd::scene
    cd::render
    cd::asset_gltf
    cd::material   # M11 W1 T1.15 — RenderBucket / bucket_for read alpha_mode predicates
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
