# engine/render/lighting_clusters — cd::lighting_clusters (phase660)
#
# 3D froxel grid for Forward+ / clustered-forward lighting.
# Sprint 1: CPU-side AABB-vs-sphere cluster assignment.
# Sprint 2 plan: GPU compute path + light culling shader.
#
# Namespace:  cd::render::lighting_clusters
# Header:     include/cd/render/lighting_clusters/LightingClusters.hpp
# Reference:  Olsson & Assarsson 2011 (Tiled Shading),
#             Olsson/Billeter/Assarsson 2012 (Clustered Deferred/Forward),
#             id Tech 6 cluster forward (Doom 2016, GDC 2016).

cd_add_library(lighting_clusters
  TYPE STATIC
  SOURCES
    src/LightingClusters.cpp
  PUBLIC_DEPS
    cd::core
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
