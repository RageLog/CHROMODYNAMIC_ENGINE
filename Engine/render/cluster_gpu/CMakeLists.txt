# engine/render/cluster_gpu — cd::cluster_gpu
#
# Vulkan compute pipeline for cluster_assign.comp (Phase 9 Sprint 2
# Wave 101). Drives the GPU side of the Forward+ light culling
# algorithm whose CPU baseline lives in cd::cluster (Wave 84-85).
#
# Depends on: cd::cluster (config + light types + ReferenceOutput
# shape), cd::rhi (backend-agnostic device + buffer + pipeline),
# cd::shader (runtime GLSL → SPIR-V compile).
#
# The GLSL source of cluster_assign.comp is embedded as a raw string
# literal in the .cpp (no filesystem dep at runtime).

cd_add_library(cluster_gpu
  TYPE STATIC
  SOURCES src/GpuPipeline.cpp
  PUBLIC_DEPS cd::cluster cd::rhi cd::shader cd::core
)
