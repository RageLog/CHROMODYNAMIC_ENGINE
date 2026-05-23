# engine/render/rhi_d3d12 — cd::rhi_d3d12
#
# Windows D3D12 backend for cd::rhi. Phase 12.B opens the boot-only
# implementation (device + adapter + queue + swapchain + clear-color
# present); subsequent v0.27.x patches add PSO, draw, buffer/texture
# upload. Pattern mirrors cd::rhi_metal scaffolding.
#
# EXCLUDE_FROM_INSTALL: the boot stub will be in the install tree
# once the surface is complete enough to promise ABI stability
# against. Until then, downstream consumers build from source.

cd_add_library(rhi_d3d12
  TYPE STATIC
  EXCLUDE_FROM_INSTALL  # boot-only; v0.27.x patches complete the surface
  SOURCES
    src/D3D12Device.cpp
    src/D3D12ShaderCompile.cpp
  PUBLIC_DEPS cd::core cd::rhi
)

# Windows-only link path. d3d12.lib + dxgi.lib + dxguid.lib are
# system-provided by the Windows SDK; no FetchContent needed. The
# guard keeps Linux/macOS builds clean (the .cpp #if-defs out the
# entire implementation off-Windows).
if(WIN32)
  # d3dcompiler is added Phase 14.C — the backend exposes a
  # `compile_hlsl()` helper so sample / app code can produce DXIL
  # blobs at runtime without dragging an SDK dep into every consumer.
  target_link_libraries(cd_rhi_d3d12 PRIVATE d3d12 dxgi dxguid d3dcompiler)
endif()
