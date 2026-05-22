# engine/render/rhi — cd::rhi (header-only Sprint S3.0)
# ADR-001 — RHI abstraction. Implementations (Vulkan, D3D12, Metal, GL) live
# in sibling targets cd_rhi_vulkan, cd_rhi_d3d12, … added in later sprints.

cd_add_library(rhi
  TYPE INTERFACE
  PUBLIC_DEPS cd::core cd::math
)

if(CD_ENABLE_TESTING)
  add_subdirectory(tests)
endif()
