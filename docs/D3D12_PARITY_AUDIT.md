# D3D12 ↔ Vulkan Backend Parity Audit

> Snapshot as of Phase 122 / v0.99.49 (2026-05-24).
> Methodology: walk every virtual on `cd::rhi::IDevice` and
> `cd::rhi::ICommandBuffer`; for each, look at the D3D12 and Vulkan
> implementations and classify as **OK / Partial / Stub**.
> "Stub" = returns `rhi_errors::Code::kNotImplemented`.

## Summary

| Backend  | `kNotImplemented` sites | Notes |
|----------|-------------------------|-------|
| Vulkan   | 3                       | Phase 17.A real BLAS; RT pipeline interface only (Phase 118) |
| D3D12    | 9                       | "boot-only" rationale documented in source |
| OpenGL   | ~30                     | Boot-only; resource paths deferred (Phase 120) |
| Metal    | ~30                     | Stub backend; documented as Phase 23+ |

## Per-method matrix

Methods are grouped by IDevice subsection.

### Buffers

| Method                  | Vulkan | D3D12   | Notes |
|-------------------------|--------|---------|-------|
| `create_buffer`         | OK     | OK      | DX12 wraps `ID3D12Resource` + GPU upload heap |
| `upload_buffer`         | OK     | OK      | CB/UB upload paths exercised by hello_d3d12_triangle |
| `download_buffer`       | OK     | OK      | Used by golden-capture |
| `destroy_buffer`        | OK     | OK      | |

### Textures

| Method                       | Vulkan | D3D12   | Notes |
|------------------------------|--------|---------|-------|
| `create_texture` (2D)        | OK     | OK      | |
| `create_texture` (cube)      | OK     | Stub    | D3D12Device.cpp:1419 — needs `D3D12_SRV_DIMENSION_TEXTURECUBE` view path |
| `create_texture` (3D)        | OK     | Partial | Allocated, no view |
| `create_texture_view`        | OK     | Stub    | D3D12Device.cpp:426 — all view dims pending |
| `upload_texture`             | OK     | OK      | Used by hello_d3d12_triangle texture path |
| `destroy_texture`            | OK     | OK      | |
| `destroy_texture_view`       | OK     | OK      | |

### Pipelines

| Method                       | Vulkan | D3D12   | Notes |
|------------------------------|--------|---------|-------|
| `create_pipeline_layout`     | OK     | OK      | Phase 14.C DX12 root signature path |
| `create_graphics_pipeline`   | OK     | OK      | DXC + SM6 path Phase 16.B |
| `create_compute_pipeline`    | OK     | OK      | |
| `create_rt_pipeline`         | Stub   | Stub    | Phase 118 interface only; both backends still kNotImplemented |
| `destroy_pipeline_*`         | OK     | OK      | |

### Descriptors

| Method                       | Vulkan | D3D12   | Notes |
|------------------------------|--------|---------|-------|
| `create_descriptor_set_layout` | OK   | OK      | |
| `allocate_descriptor_set`    | OK     | OK      | Phase 16.B DX12 descriptor heap |
| `update_descriptor_set`      | OK     | Partial | D3D12Device.cpp:1065 — input attachment / cube SRV not wired |
| `free_descriptor_set`        | OK     | OK      | |

### Acceleration structures (Phase 17.A)

| Method                         | Vulkan | D3D12 | Notes |
|--------------------------------|--------|-------|-------|
| `create_acceleration_structure` (BLAS) | OK     | Stub  | Vulkan via KHR_acceleration_structure |
| `create_acceleration_structure` (TLAS) | Stub   | Stub  | Phase 117 interface only — Phase 118 candidate |
| `build_acceleration_structure` | Partial| Stub  | Vulkan BLAS only |
| `destroy_acceleration_structure` | OK   | OK    | |

### Synchronization

| Method                       | Vulkan | D3D12   | Notes |
|------------------------------|--------|---------|-------|
| `create_semaphore`           | OK     | Stub    | D3D12 1074 |
| `create_fence`               | OK     | Stub    | D3D12 1078 |
| `wait_for_fence`             | OK     | Stub    | D3D12 1083 |
| `create_timeline_semaphore`  | OK     | Stub    | D3D12 1093 |
| `wait/signal_timeline_semaphore` | OK | Stub    | D3D12 1097 / 1105 |

### Swapchain

| Method               | Vulkan | D3D12 | Notes |
|----------------------|--------|-------|-------|
| `create_swapchain`   | OK     | OK    | DXGI tearing flag + RGBA/sRGB matrix Phase 14.C |
| `acquire_next_image` | OK     | OK    | |
| `present`            | OK     | OK    | |

### CommandBuffer (ICommandBuffer)

| Method                              | Vulkan | D3D12   | Notes |
|-------------------------------------|--------|---------|-------|
| `begin` / `end`                     | OK     | OK      | |
| `begin_render_pass` / `end_render_pass` | OK | OK    | DX12 wraps OMSetRenderTargets manually |
| `bind_graphics_pipeline`            | OK     | OK      | |
| `bind_compute_pipeline`             | OK     | OK      | |
| `bind_vertex_buffer` / `bind_index_buffer` | OK | OK   | |
| `draw` / `draw_indexed`             | OK     | OK      | hello_d3d12_triangle covers |
| `dispatch`                          | OK     | OK      | |
| `dispatch_indirect`                 | OK     | Partial | DX12 sig table partial |
| `push_constants`                    | OK     | OK      | Root constants Phase 14.C |
| `set_viewport` / `set_scissor`      | OK     | OK      | |
| `barrier`                           | OK     | OK      | |
| `copy_buffer` / `copy_image_to_buffer` | OK | OK     | |
| `build_acceleration_structure`      | Partial| Stub    | Vulkan BLAS only |
| `dispatch_rays`                     | Stub   | Stub    | Phase 118 interface only |

## Outstanding D3D12 gaps (Phase 122 priority list)

1. **`create_texture_view`** — every dimension stub. Needed before any
   sampling or attachment view on D3D12 works at parity with Vulkan.
2. **Cube-map SRV** (D3D12Device.cpp:1419) — IBL sample needs this.
3. **Fences / Semaphores / Timeline semaphores** — five stubs. D3D12
   has `ID3D12Fence` already; just needs Result wrapping +
   `WaitForSingleObject` on a fence event.
4. **Input-attachment / cube SRV in `update_descriptor_set`**
   (line 1065) — bundle with #1 and #2.
5. **Acceleration structures** — DXR Tier 1.1 is supported but the
   backend stubs all four entry points. Lift from
   `ID3D12Device5::CreateStateObject` after Phase 118 implementation.

## Outstanding Vulkan gaps (Phase 117/118 candidates)

1. **TLAS create + build** — descriptor in place (Phase 117);
   implementation requires `vkGetAccelerationStructureBuildSizesKHR`
   for type `kTopLevel`, instance buffer upload, BLAS device-address
   resolution, scratch buffer alloc.
2. **`create_rt_pipeline`** — `vkCreateRayTracingPipelinesKHR` + SBT
   layout + shader-group binding handles.
3. **`dispatch_rays`** — `vkCmdTraceRaysKHR` with the four
   `VkStridedDeviceAddressRegionKHR` regions Phase 118 added.

## OpenGL gaps (Phase 120 long-term)

OpenGL backend is intentionally boot-only as of v0.49.0 — pending a
full WGL/GLX extension-loading pass + GL function dispatch table.
Resource paths (buffer / texture / swapchain / pipeline) all return
`kNotImplemented`. Mobile-OpenGL parity is the lowest-priority lane
in the marathon's outstanding list (user-deprioritised).

## Recommended next phases

| Suggested phase | Effort  | Unblocks |
|-----------------|---------|----------|
| D3D12 create_texture_view | 2-4 hours | Texture sampling on DX12, golden-capture parity |
| D3D12 fence / semaphore wrappers | 2-3 hours | Frame pacing on DX12, ImGui DX12 path |
| Vulkan TLAS build path | 4-6 hours | hello_rt real ray dispatch |
| Vulkan RT pipeline create | 4-6 hours | dispatch_rays |
| D3D12 cube-map view | 1-2 hours | IBL on DX12 |
