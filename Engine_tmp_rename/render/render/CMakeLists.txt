# engine/render/render — cd::render (Sprint S3.9)
#
# High-level renderer that orchestrates per-frame command buffers, sync
# primitives, swapchain acquire/present, and implicit image-layout
# transitions. Sits on top of cd::rhi only — cd::material is a peer (the
# renderer doesn't dictate materials).

cd_add_library(render
  TYPE STATIC
  SOURCES
    src/Renderer.cpp
  PUBLIC_DEPS
    cd::rhi
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
