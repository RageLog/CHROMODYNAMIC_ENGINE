# engine/render/rhi_native — cd::rhi_native
#
# Unified GPU backend dispatcher (Wave 97). Links the available
# concrete backends and exposes `cd::rhi::make_native_device()`
# which selects the best one for the host platform.
#
# Backends conditionally linked:
#   * cd::rhi_vulkan  — always available (volk loader gates ICD)
#   * cd::rhi_metal   — built on every platform (stub on non-Apple)
#   * cd::rhi_d3d12   — built on every platform (stub on non-Windows)

set(_RHI_NATIVE_DEPS cd::core cd::rhi cd::rhi_metal cd::rhi_d3d12)
if(TARGET cd_rhi_vulkan)
  list(APPEND _RHI_NATIVE_DEPS cd::rhi_vulkan)
endif()

cd_add_library(rhi_native
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # transitive on cd::rhi_vulkan (vendored volk/VMA)
  SOURCES src/NativeDevice.cpp
  PUBLIC_DEPS ${_RHI_NATIVE_DEPS}
)
