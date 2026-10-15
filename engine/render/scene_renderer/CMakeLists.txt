# engine/render/scene_renderer — cd::scene_renderer (FAZ 4 Waves 4.1–4.4)
# Unified GPU-Driven & Photorealistic SceneRenderer FrameGraph Pipeline.

cd_add_library(scene_renderer
  TYPE STATIC
  SOURCES
    src/SceneRenderer.cpp
  PUBLIC_DEPS
    cd::core
    cd::rhi
    cd::framegraph
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
