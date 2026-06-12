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
    src/RtClosestHit.cpp
    src/UiVariant.cpp
  PUBLIC_DEPS
    cd::rhi
    cd::shader
    cd::core
    cd::asset   # PbrParams.hpp references AssetId
    cd::light   # Phase 171 — LitPbrMaterial packs cd::light::Light
    cd::anim    # Phase 228 / SK6 — SkinnedLitMaterial re-exports cd::anim VS
  PRIVATE_DEPS
    cd::gluon   # SL-D wave 3 (ADR-20260612 addendum) — Material.cpp builds the
                # default ModuleResolver bridge; no material header names a
                # gluon type, so the dependency stays PRIVATE.
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
