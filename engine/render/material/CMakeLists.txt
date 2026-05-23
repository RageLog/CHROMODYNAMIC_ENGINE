# engine/render/material — cd::material (Sprint S3.8)
#
# Bundles a graphics pipeline + descriptor set layout + shader pair into a
# single owned object (`Material`) and a per-use descriptor set
# (`MaterialInstance`). Depends on cd::rhi for GPU lifecycle and on
# cd::shader for GLSL → SPIR-V compilation (the shader dependency is
# optional from the call site: pre-compiled SPIR-V works without a
# compiler).

cd_add_library(material
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # transitive on cd::shader (vendored glslang)
  SOURCES
    src/Material.cpp
  PUBLIC_DEPS
    cd::rhi
    cd::shader
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
