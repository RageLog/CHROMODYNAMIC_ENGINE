# engine/render/cluster_fx — cd::cluster_fx (Phase 234 umbrella)
#
# Aggregates the three cluster libs:
#   cd::cluster      (CPU grid build + reference compute)
#   cd::cluster_gpu  (Vulkan compute pipeline wiring)
#   cd::cluster_pbr  (per-pixel cluster lookup helper for PBR FS)
cd_add_library(cluster_fx
  TYPE INTERFACE
  EXCLUDE_FROM_INSTALL    # transitive on cd::cluster_gpu (glslang link)
  PUBLIC_DEPS
    cd::cluster
    cd::cluster_gpu
    cd::cluster_pbr
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
