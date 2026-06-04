# engine/render/lighting_clusters — cd::lighting_clusters (phase660 + phase672)
#
# 3D froxel grid for Forward+ / clustered-forward lighting.
# Sprint 1 (phase660): CPU-side AABB-vs-sphere cluster assignment.
# Sprint 2 (phase672): GPU compute light-culling pass (kClusterCullCS).
#
# Namespace:  cd::render::lighting_clusters
# Headers:    include/cd/render/lighting_clusters/LightingClusters.hpp
#             include/cd/render/lighting_clusters/DispatchPass.hpp
# Reference:  Olsson & Assarsson 2011 (Tiled Shading),
#             Olsson/Billeter/Assarsson 2012 (Clustered Deferred/Forward),
#             id Tech 6 cluster forward (Doom 2016, GDC 2016).
#
# Sprint-2 promotes the library's link surface to include cd::rhi (public,
# since DispatchPass.hpp returns BufferHandle / ComputePipelineHandle) and
# cd::shader (private, only the .cpp compiles GLSL via glslang). Mirrors
# cd::restir_di / cd::ddgi which made the same conversion to keep cd::shader
# transitively private to consumers that only need the CPU helpers.

cd_add_library(lighting_clusters
  # cd::shader (glslang) is EXCLUDE_FROM_INSTALL because of vendored
  # FetchContent deps; transitively any library that depends on it must also
  # opt out of the install export set, otherwise CMake refuses to generate.
  EXCLUDE_FROM_INSTALL
  TYPE STATIC
  SOURCES
    src/LightingClusters.cpp
    src/DispatchPass.cpp
  PUBLIC_DEPS
    cd::core
    cd::rhi
  PRIVATE_DEPS
    cd::shader
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
