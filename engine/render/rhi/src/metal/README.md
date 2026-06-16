# cd::rhi — Metal backend

**Status**: IMPL ~60% (impl-complete-on-paper) | GPU-verified 0% (Mac-gated hard blocker)

Two-axis: *impl* = code written + structural review passed on Windows; *gpu-verified* = compiled on Apple Clang + confirmed on Apple-Silicon GPU. The backend has **never been compiled by Apple Clang and has never touched a real GPU.** Every GPU-verification item is Mac-gated.

---

## What is implemented (structural / Windows-verifiable)

The `.mm` source is a full-body implementation that compiles only when `CD_RHI_METAL_ENABLED=ON` on macOS. The Windows build compiles `MetalDevice.cpp` (non-Apple stub → `kBackendInitFailed`; M-CPP-STUB hardened in Wave 0).

| Surface | Impl (source) | GPU-verified | Notes |
|---|---|---|---|
| Device + queue | Yes | No (mac-gated) | `MTLDevice` + `MTLCommandQueue` init; feature-flag population incl. `ray_tracing` from `supportsRaytracing` (line 569). |
| Resources (buffer / texture) | Yes | No | `MTLBuffer` / `MTLTexture` create; BCn pitch fix (M-BCN-PITCH, Wave 0). |
| Graphics + compute pipelines | Yes | No | `MTLRenderPipelineDescriptor` / `MTLComputePipelineDescriptor`. |
| Mesh-shader pipeline | Yes (gated) | No | `!features_.mesh_shader` gate + `@available(macOS 13)` OS guard (lines 1032/1098). |
| Descriptors + bindless | Yes (gated) | No | Argument-buffer Tier 2 path (`create_bindless_texture_array` line 1430). Tier-1 fork decision deferred to D-ARGBUF-TIER (needs real device). |
| Render pass | Yes | No | `MTLRenderPassDescriptor`; `begin_render_pass` in `MetalCommandBuffer.mm`. |
| MSAA resolve | Draft | No | `resolveTexture` / `MultisampleResolve` draft (M-MSAA-RESOLVE, Wave 4); mac-gated verify. |
| Stencil ops | Draft | No | Front/back stencil ops draft (M-STENCIL, Wave 4). |
| Depth bias | Draft | No | Slope-scale / clamp / `MTLDepthClipMode` draft (M-DEPTHBIAS, Wave 4). |
| HDR / EDR | Draft | No | `CAMetalLayer` EDR metadata draft (M-HDR-EDR, Wave 4); mac + HDR panel gated. |
| Swapchain | Yes | No | `CAMetalLayer` backed; HDR colour-space path is the draft. |
| Sync (events / fences) | Yes | No | `MTLEvent` / `MTLFence`; `signal_counter_` hardened to `std::atomic<uint64_t>` (M-EVENT-ATOMIC, Wave 0). |
| RT — inline ray-query (production) | Yes (gated) | No | `id<MTLAccelerationStructure>` BLAS/TLAS build (line 1306); `metal::raytracing::intersector<>` via SPIRV-Cross lowering. `!features_.ray_tracing` gate (line 1325). |
| RT — SBT pipeline | Absent by design | n/a | Not implemented. Falls to `IDevice` base default → `kNotImplemented`. Closed as out-of-charter. See §B-RT-SCOPE. |
| Parallel render pass | Yes | No | `MTLParallelRenderCommandEncoder` path authored; mac-gated run. |
| Readback (blocking) | Yes | No | `copyFromBuffer` / map path; BCn-pitch formula hardened (M-BCN-PITCH). |
| Debug groups | Yes | No | `pushDebugGroup:` / `popDebugGroup`. |
| NDC-Y convention | Yes | No | Negative-viewport-height (matching Vulkan Y-down) wired per `ADR-20260615-ndc-y-handedness.md`. |
| MSL shader toolchain (Windows verify) | Yes | Yes | `cd_test_metal_shader_toolchain` (11/11 PASS on Windows): binding-index disjointness, push-constant layout, MSL output sanity. |

### kNotImplemented sites (all feature-gates, zero TODO-holes)

Five return sites in `MetalDevice.mm` / `MetalDevice.cpp` (plus one dead `.cpp` text never linked on Apple). All are `!features_.<cap>` or `@available` OS guards. Full enumeration: `docs/RHI_KNOTIMPL_INVENTORY.md §Metal`.

---

## Intentionally deferred / out-of-scope

| Item | ADR section | Reason |
|---|---|---|
| SBT RT-pipeline (`create_rt_pipeline` / `dispatch_rays`) | §B-RT-SCOPE | Production RT path is inline ray-query (present and gated). SBT needs visible-function-table + intersection-function-table, zero engine consumers. |
| Argument-buffer Tier-1 fallback | D-ARGBUF-TIER | Fork decision needs a real device to confirm Apple-Silicon-only (Tier 2 always present). |
| Async / fenced non-blocking readback | §B-NULL-NONBLOCK-READBACK | Blocking path covers current needs. |
| Sparse / tiled resources | §B-SPARSE | No virtual-texture consumer yet. |

ADR: `docs/ADR/ADR-20260616-backend-wontfix-decisions.md`

---

## Remaining GPU verification (Mac-gated hard blockers)

All items below require an **Apple-Silicon Mac** (or self-hosted Apple-Silicon CI runner — GitHub-hosted `macos-14` cannot access a Metal GPU) plus **Fork-A sign-off**. No amount of code on this Windows host can retire them.

| Step | ID | What happens |
|---|---|---|
| First Apple Clang compile | D-METAL-COMPILE | `CD_RHI_METAL_ENABLED=ON` configure + `-Werror` clean. Until this passes there is zero evidence the `.mm` compiles. |
| 9 Tier-2 GPU test binaries | D-METAL-GPU | device / buffer / texture / pipeline / shader / descriptor / barrier / swapchain / RT — authored behind `#if __APPLE__`; run on-device. |
| Cull-parity test | D-METAL-GPU | Port of `test_d3d12_face_cull_parity`; locks NDC-Y + winding-inversion story. |
| Windowed `hello_metal` + MTL validation | D-METAL-GPU | End-to-end boot → first triangle, validation-clean. |
| Draft features (MSAA / stencil / depth-bias / HDR-EDR) | D-METAL-GPU | Pixel-accurate confirmation of Wave 4 drafts. |
| Cross-backend golden arm | D-METAL-PARITY | Add Metal arm to the Vulkan/D3D12 cross-backend pixel matrix (currently absent). |
| Argument-buffer Tier decision | D-ARGBUF-TIER | Confirm device tier → assert Tier 2 or add Tier 1 fallback. |

Full procedure: `docs/METAL_MAC_TESTING.md`
Roadmap: `docs/ROADMAP_BACKEND_TO_100.md §GROUP D (Wave 5)`

---

## Key files

| File | Role |
|---|---|
| `MetalDevice.mm` | Device, all resource creation, pipelines, AS build, swapchain, feature detection |
| `MetalCommandBuffer.mm` | Command encoding, render pass, barriers, RT acceleration-structure commands |
| `MetalPipeline.mm` | Render + compute pipeline state descriptors, depth/stencil/raster state |
| `MetalSwapchain.mm` | `CAMetalLayer` lifecycle, drawable acquisition, present |
| `MetalInternal.hpp` | Internal types, `MTLEvent` wrapper, `signal_counter_` (atomic hardened) |
| `MetalDevice.cpp` | Non-Apple stub (Windows build path) — returns `kBackendInitFailed` |
| `MetalShaderToolchain.cpp` | MSL-toolchain verification (Windows-runnable; 11/11 PASS) |

## Related ADRs

| ADR | Topic |
|---|---|
| `ADR-001-rhi-architecture.md` | RHI interface contract |
| `ADR-20260530-metal-backend.md` | Metal backend design, binding model, task DAG |
| `ADR-20260615-metal-backend-completion.md` | M1–M12 implementation record; Wave 4 scope; Fork-A pre-flight |
| `ADR-20260615-ndc-y-handedness.md` | NDC-Y / clip-space convention and Metal viewport-height fix |
| `ADR-20260616-backend-wontfix-decisions.md` | §B-RT-SCOPE (SBT absent by design), §B-SPARSE, §B-NULL-NONBLOCK-READBACK |
| `ADR-20260606-W8-BE-rt-texture-sampling-bindless.md` | Bindless texture architecture; cross-encoder visibility rule (Metal lesson §256) |
