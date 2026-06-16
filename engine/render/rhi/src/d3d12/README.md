# cd::rhi — D3D12 backend

**Status**: IMPL ~90% | GPU-verified ~90% (RTX 3080 + WARP on this host)

Two-axis: *impl* = code written and on the parity bar; *gpu-verified* = pixels/traces confirmed on real hardware or WARP software adapter.

The D3D12 backend is byte-exact parity with Vulkan on every implemented surface (confirmed by `test_backend_pixel_parity` + `test_d3d12_face_cull_parity`).

---

## What is implemented

| Surface | Impl | GPU-verified | Notes |
|---|---|---|---|
| Resources (buffer / texture / views) | Yes | Yes | Descriptor heap management; `D3D12MA` backing. |
| Texture format table | Partial | Yes | ~30 of 43 `cd::rhi::Format` values mapped. BCn + some depth variants added Wave 1. Full 43-format coverage in progress (D-FORMAT-MAP). |
| Graphics + compute pipelines | Yes | Yes | `ID3D12PipelineState` via `CreateGraphicsPipelineState` / `CreateComputePipelineState`. |
| Mesh-shader pipeline | Yes (gated) | Yes | `!features_.mesh_shader \|\| !device2_` gate (SM 6.5 / Tier 1 check). |
| Pipeline cache | Partial | Partial | `ID3D12PipelineLibrary` (`D3D12PipelineLibrary.hpp` present); not yet fully wired end-to-end for all pipeline types. |
| Descriptors + bindless | Yes | Yes | Resource-binding Tier 2 check; `create_bindless_texture_array` via heap offset. §B-D3D12-STRUCTURED: all engine SSBOs are raw `ByteAddressBuffer`. |
| Sync — timeline / fence | Yes | Yes | `ID3D12Fence` is the real cross-queue primitive. |
| Sync — binary semaphores | No-op | n/a | Binary semaphores are inert; cross-queue sync uses fence values. §B-D3D12-SYNC. |
| Swapchain + present | Yes | Yes | `IDXGISwapChain4`; `kSwapchainOutOfDate` + `ResizeBuffers` wired (D-SWAPCHAIN-RESIZE, Wave 1). |
| HDR colour-space | Yes | Yes | `SetColorSpace1` + `CheckColorSpaceSupport` wired (D-HDR-SWAPCHAIN, Wave 1). Pixel eyeball needs HDR panel. |
| Render pass / StoreOp | Partial | Yes | `kDontCare` StoreOp is a no-op (contents always preserved). Correctness-identical to Vulkan. §B-D3D12-STOREOP. |
| MSAA — Texture2DMS SRV | Yes | Yes | SRV path branches to `DIMENSION_TEXTURE2DMS` for MS textures (D-SRV-MS, Wave 1). |
| Parallel render pass | Yes | Yes | Thread-per-lane deferred command lists replayed in lane order onto primary. Byte-identical output to single-thread. §B-D3D12-PARALLEL. |
| Readback (blocking) | Yes | Yes | `copy_image_to_buffer` with de-pitch (row-padding removal); confirmed via `test_backend_pixel_parity`. |
| Debug groups + depth | Yes | Yes | `debug_group_depth_` tracked; `PIXBeginEvent` / `PIXEndEvent` path. |
| RT — inline ray-query (production) | Yes | Yes | DXR 1.1 `RayQuery`; BLAS/TLAS `BuildRaytracingAccelerationStructure`; `CreateStateObject` for ray-query PSO. |
| RT — SBT pipeline (extra) | Yes | Yes | `CreateStateObject(RTPSO)` (line 2969) + `dispatch_rays` → `DispatchRays` (line 7677). Real `GetShaderIdentifier` SBT author. Consumed by `hello_path_trace`. |
| RT — indexed BLAS | Yes | Yes | `IndexFormat` + `IndexBuffer` GVA wired (D-BLAS-INDEXED, Wave 1). |
| AS build / compaction / refit | Yes | Yes | `EmitRaytracingAccelerationStructurePostbuildInfo` + `CopyRaytracingAccelerationStructure(COMPACT)`; `PERFORM_UPDATE` refit. |
| Indirect draw / dispatch | Yes | Yes | `ExecuteIndirect` with `CommandSignature`; `draw_indirect`, `draw_indexed_indirect`, `dispatch_indirect`. |
| GPU query subsystem | Yes | Yes | `CreateQueryHeap` + `ResolveQueryData`; timestamp + pipeline-statistics. |
| Copy image → image | Yes | Yes | `CopyResource` / `CopyTextureRegion` paths. |
| Per-subresource state tracking | Yes | Yes | Each mip/layer gets its own `ResourceState` record; `ALL_SUBRESOURCES` whole-resource fast path retained (D-MIPSTATE, Wave 1). |
| Root-signature push cost guard | Yes | Yes | Returns `kInvalidArgument` instead of silently clamping when root-cost > 64 DWORDs (D-ROOTCOST, Wave 1). |

### kNotImplemented sites (all are feature-gates, zero TODO-holes)

Six return sites in `D3D12Device.cpp` — every one is a `!features_.<cap>` or unknown-enum guard with a real body below. Full enumeration: `docs/RHI_KNOTIMPL_INVENTORY.md §D3D12`.

---

## Intentionally deferred / out-of-scope

| Item | ADR section | Reason |
|---|---|---|
| StoreOp / kDontCare | §B-D3D12-STOREOP | Perf-only hint; D3D12 always preserves. No correctness impact. |
| kStorageBuffer StructuredBuffer path | §B-D3D12-STRUCTURED | All engine SSBOs are raw ByteAddress; no stride-field on the interface. |
| Parallel pass true-secondary throughput | §B-D3D12-PARALLEL | Sequential-replay is correctness-correct; peak-replay delta is not on the bar. |
| Binary semaphore semantics | §B-D3D12-SYNC | Timeline/fence is the real primitive; no engine path needs binary-sema on D3D12. |
| Async / fenced non-blocking readback | §B-NULL-NONBLOCK-READBACK | Blocking path covers current needs. |
| Sparse / tiled resources | §B-SPARSE | No virtual-texture consumer yet. |
| OpenGL backend | §B-OPENGL | Permanently out-of-charter (Vulkan → D3D12 → Metal). |

ADR: `docs/ADR/ADR-20260616-backend-wontfix-decisions.md`

---

## Remaining verification

- **Pixel-parity test** (D-D3D12-PARITY-RUN): `test_backend_pixel_parity` + `test_d3d12_{face_cull,msaa,sampler}_parity` — runnable now on the RTX 3080. Converting D3D12 from "code-complete" to "fully GPU-verified" is Wave 2, the single highest-value remaining action.
- **DXR dispatch** (D-DXR-RUN): `CreateStateObject → DispatchRays` + compaction/refit ray-equivalence on RTX 3080 — also runnable now.
- **NVIDIA self-hosted runner** (D-NVIDIA-RUNNER): registration makes DXR a standing CI gate rather than a manual run.
- **Full format matrix** (D-FORMAT-MAP + C-FORMAT-MATRIX): a per-backend format conformance test guards `create_texture` divergence.

Roadmap: `docs/ROADMAP_BACKEND_TO_100.md §GROUP D`

---

## Key files

| File | Role |
|---|---|
| `D3D12Device.cpp` | Device, all resource creation, pipelines (gfx/compute/mesh/RT), AS, swapchain, query heaps, command-queue management |
| `D3D12ShaderCompile.cpp` | DXC-based HLSL / DXIL compilation |
| `D3D12ShaderToolchain.cpp` | Shader toolchain selection (dxcompiler.dll discovery) |

## Related ADRs

| ADR | Topic |
|---|---|
| `ADR-001-rhi-architecture.md` | RHI interface contract |
| `ADR-20260614-d3d12-binding-model.md` | Root-signature space-per-set layout; push-constant cost contract |
| `ADR-20260615-ndc-y-handedness.md` | NDC-Y convention (negative-viewport-height on D3D12 matches Vulkan Y-down) |
| `ADR-20260616-backend-wontfix-decisions.md` | All CLOSED decisions (StoreOp, structured-buffer, parallel, sync, …) |
| `ADR-20260606-W8-BE-rt-texture-sampling-bindless.md` | Bindless texture architecture (dedicated descriptor set rule) |
| `ADR-20260612-x4-d3d12-shader-toolchain.md` | DXC integration and DXIL shader pipeline |
