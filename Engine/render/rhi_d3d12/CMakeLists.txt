# engine/render/rhi_d3d12 — cd::rhi_d3d12
#
# Windows D3D12 backend for cd::rhi. Skeleton (Wave 97); full impl
# Phase 9. Pattern mirrors cd::rhi_metal.

cd_add_library(rhi_d3d12
  TYPE STATIC
  SOURCES src/D3D12Device.cpp
  PUBLIC_DEPS cd::core cd::rhi
)

# On Windows the implementation links d3d12 + dxgi (and dxguid for
# IID symbols). Phase 9 enables this; Wave 97 stub doesn't link any
# system libs yet.
if(WIN32)
  # Stub TU doesn't reference Win32 D3D12 symbols, so no link needed.
  # Phase 9 wiring will add:  target_link_libraries(cd_rhi_d3d12 PRIVATE d3d12 dxgi dxguid)
endif()
