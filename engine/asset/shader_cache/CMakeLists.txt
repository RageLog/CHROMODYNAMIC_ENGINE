# engine/asset/shader_cache — cd::asset_shader_cache (Phase 609)
#
# Asset-tier in-process SPIR-V blob cache.
# Additive sidecar of cd::shader (not a version bump; no dependency on glslang).
#
# Depends on:
#   cd::core    — Defines.hpp, platform macros
#   cd::asset   — INTERFACE asset layer (IAssetLoader etc.)
#   cd::shader  — ShaderStage enum (INTERFACE dep; callers may pass stage ints)
#
# Note: cd::shader is listed as a PUBLIC dep so consumers of this library
# can use the ShaderStage values when building ShaderKey without an extra
# target_link_libraries on their side.  The cache itself never calls into
# glslang / ICompiler — the dependency is purely for the shared type vocabulary.

cd_add_library(asset_shader_cache
  TYPE STATIC
  SOURCES
    src/ShaderCache.cpp
  PUBLIC_DEPS
    cd::core
    cd::asset
    cd::shader
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
