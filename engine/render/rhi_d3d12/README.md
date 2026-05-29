# cd::rhi_d3d12

**Purpose**: Direct3D 12 backend implementation for the GPU hardware abstraction layer (RHI). Provides GPU device enumeration, swapchain, and command submission on Windows via the DirectX Runtime and DirectX Shader Compiler (DXC).

**Namespace**: `cd::rhi_d3d12`.

**Platform**: Windows only. On non-Windows platforms, a stub factory returns nullptr.

**Key Dependencies**:
- `d3d12.lib`, `dxgi.lib`, `dxguid.lib` — Windows SDK system libraries.
- `d3dcompiler.dll` (SM5.1) and `dxcompiler.dll` (SM6.x) — DXC redistributables from Windows SDK.

**Primary Types** (Internal; exposed via `cd::rhi::make_native_device()`):
- `D3D12Device` — concrete `IDevice` impl; GPU adapter selection, device creation, queue management.
- `D3D12ShaderCompile` — shader compilation wrappers for DXC; HLSL → SPIR-V / DXIL.
- `D3D12CommandBuffer` — concrete `ICommandBuffer` impl; command list recording + submission.

**Build**:
```bash
cmake --build --preset ninja-debug --target cd_rhi_d3d12
ctest --preset ninja-debug -R rhi_d3d12 --output-on-failure
```

**DXC Distribution**:
- CMake copies `dxcompiler.dll` and `dxil.dll` from the Windows SDK into the output directory so runtime symbol lookup succeeds without PATH manipulation.
- If the SDK doesn't include DXC (older installations), compilation continues cleanly with SM5.1 via d3dcompiler.

**Maturity**:
- Phase 12.B: boot-only (device + adapter + queue + swapchain + clear-color present).
- Phase 15+: PSO, draw, buffer/texture upload; full surface mirrors cd::rhi_metal progression.
- Not yet in install tree pending ABI stability guarantee.

**Notes**:
- Reverse-Z depth compare (standard across all RHI backends).
- Command list reuse via command allocator pools for low-latency frame submission.
