# ROADMAP — Backend Completion (Finish All Backend Implementation, Forever)

> Goal: complete ALL RHI backend IMPLEMENTATION (Vulkan + D3D12 + Metal) so we never do backend-implementation work again. After this, only **tests** and **Mac-GPU verification** of Metal may remain, then we move to feature work (X5 shader hot-reload, DDGI).
>
> Source: 3 per-backend deep audits (2026-06-15), each impl-now claim re-verified against source by the synthesizing architect.

## 0. ✅ STATUS: ALL IMPL-NOW DONE (2026-06-15) — BACKEND IMPLEMENTATION COMPLETE

Every impl-now item below is closed. The 3 RHI backends are implementation-complete;
the ONLY remaining work is tests + Metal Mac-GPU verification + documented intentional-defers.

| Item | Backend | Status | Commit |
| --- | --- | --- | --- |
| (none — already complete) | Vulkan | ✅ phase1183 TAM | — |
| D1 MSAA SampleCount | D3D12 | ✅ DONE + bidirectional test | phase1206 `7c74702` |
| D3 8 DeviceFeatures flags | D3D12 | ✅ DONE + test | phase1206 `7c74702` |
| D4 storage-image UAV dim | D3D12 | ✅ DONE + test | phase1206 `7c74702` |
| D2 sampler bindability | D3D12 | ✅ DONE + bidirectional test | phase1207 `7ed9e51` |
| (winding-compensation) | D3D12 | ✅ DONE + test (pre-plan) | phase1204 `57d283b` |
| M6 dispatch threadgroup | Metal | ✅ DONE — host-tested reflection + .mm | phase1208 `02cc47b` |
| M-caps limits/features | Metal | ✅ DONE (.mm) | phase1208 `02cc47b` |
| M-readback device-copy | Metal | ✅ DONE (.mm) | phase1208 `02cc47b` |
| M10 mesh-shader | Metal | ✅ DONE — host-tested MSL lowering + .mm | phase1209 `7448759` |
| M11 bindless array | Metal | ✅ DONE (.mm) | phase1209 `7448759` |
| winding-compensation | Metal | ✅ DONE (.mm, mirrors D3D12) | phase1209 `7448759` |
| M12 windowed sample + acquire/swapchain | Metal | ✅ DONE (.mm + Windows stub) | phase1209 `7448759` |
| macOS CI skeleton | Metal | ✅ DONE | phase1209 `7448759` |

**Metal structural quality gate (Mac can't GPU-test → adversarial review is the gate):**
architect review = **SHIP** (0 bugs / 7 categories: override-signatures exact, @available
self-consistent, dispatch group-count correct, winding symmetric+single-applied, bindless
own-set, transient swapchain handle no-double-free, binding map respected). safety-integration
review = **SHIP** (M9-class residency does NOT recur — every bindless slot made resident;
ARC/lifetime, dispatch zero-group, mesh-PSO aliasing, acquire-signal monotonicity, new-state
locking all sound).

**ALLOWED RESIDUE (the only backend work that legitimately remains):**

1. **Tests** — D3D12 has bidirectional parity tests for all 4 fixes + M6/M10 have host MSL tests;
   remaining = Metal GPU-side gtests (Mac-gated) + a Metal cull-parity GPU test (D3D12 analog).
2. **Mac-GPU verification (M0)** — build all `.mm` with `CD_RHI_METAL_ENABLED=ON` on macOS +
   GPU-run; see `docs/METAL_MAC_TESTING.md`. The code is written + structurally reviewed now.
3. **Intentionally-deferred** (documented decisions, NOT gaps): AS compaction/refit (V3),
   SBT-vs-ray-query (engine uses inline ray-query), indirect draw (out-of-scope, not in the
   interface). One future-hardening note: make `MetalEventObj::signal_counter_` atomic IF
   multi-thread acquire/submit is ever added (not a regression today).

**⇒ Backend implementation is FROZEN. Next work is feature-tier (X5 shader hot-reload, DDGI).**

## 1. Per-Backend Completion Status

### Vulkan — IMPLEMENTATION-COMPLETE (nothing to do)
phase1183 "TAM". Every pure-virtual in `IDevice.hpp` / `ICommandBuffer.hpp` / `IDrawRecorder` has a real, non-stub Vulkan override, and every used non-pure default is genuinely overridden: full resource lifecycle, graphics/compute/mesh pipelines, descriptor sets (AS + bindless writes), binary+timeline semaphores, fences, swapchain acquire/present, staging, `VkPipelineBarrier2` (depth-aware aspect), multi-queue `VkSubmitInfo2`, secondary-CB parallel render passes (X1-FU-F), bindless texture arrays (`descriptor_indexing`), AND a complete RT-pipeline+SBT path PLUS BLAS/TLAS build + ray-query. **Zero impl-now gaps.** All residual absences are intentionally-deferred (AS compaction/refit, SBT-vs-rayquery) or out-of-scope (indirect draw — not in the interface at all). The two `kNotImplemented` sites are a compile-time platform `#else` guard (Win32 path active) and capability gates.

### D3D12 — STRUCTURALLY COMPLETE, 4 PARITY BUGS (impl-now)
All 50+ `IDevice` virtuals + all `ICommandBuffer`/`IDrawRecorder` virtuals are real overrides. DXR (real `D3D12_STATE_OBJECT`, ShaderIdentifier copy, `dispatch_rays`, BLAS/TLAS), mesh shaders (`DispatchMesh`), bindless (dedicated heap sub-region), all copy/readback paths, full PSO state, timeline+binary semaphores+host fence — all genuinely wired. Remaining work is a cluster of **silent-ignore parity divergences vs the Vulkan reference**, none a missing override:
- **D1 (HIGH):** MSAA `SampleCount` hardcoded 1x in `create_texture` + graphics/mesh PSO (Vulkan honors `desc.samples`).
- **D2 (HIGH):** `create_sampler` descriptors land in a non-shader-visible heap and are **never bound** — all dynamic sampling silently resolves to a baked LINEAR-clamp default.
- **D3 (MED):** 8 `DeviceFeatures` flags (`ray_query`, `geometry_shader`, `tessellation_shader`, `sampler_anisotropy`, `depth_clamp`, `dual_source_blend`, `timestamp_queries`, `pipeline_statistics_queries`) left default-false despite D3D12 support. **Verified**: only `ray_tracing`/`mesh_shader`/`bindless_resources` are set; grep for the others returns no matches.
- **D4 (MED):** `kStorageImage` UAV hardcodes `ViewDimension=TEXTURE2D`, mis-dimensioning 3D/cube/array storage images (SRV path already branches correctly).

Intentionally-deferred (documented, benign): `kStorageBuffer` raw-only, `StoreOp`/`kDontCare` not consumed, sequential-replay parallel-pass.

### Metal (`cd::rhi_metal`) — NOT COMPLETE, 7 impl-now items
M1-M9 (phases 1197-1202) genuinely landed: real `MTLBuffer`/`MTLTexture`, desc-driven graphics pipeline, SPIR-V->MSL toolchain (host-complete + Windows-tested), argument-buffer descriptor writes, hazard barriers, multi-color+depth render pass, AS build + ray-query. What remains is write-the-code-now-on-Windows (behind `CD_RHI_METAL_ENABLED`, structural review now, Mac-GPU verify later):
- **M6 (HIGH, correctness bug):** `dispatch()` hardcodes `threadsPerThreadgroup=(1,1,1)` — any `local_size>1` shader runs 1 thread/group. `ComputePipelineDesc` has no workgroup field, so the fix derives `LocalSize` from SPIRV-Cross reflection.
- **M10 (MED):** `create_mesh_pipeline` + `draw_mesh_tasks` — no override (falls to `kNotImplemented`/no-op).
- **M11 (MED):** bindless texture array lifecycle (`create`/`write`/`destroy` + `bind`) — no override.
- **M-readback (MED):** device-level `copy_image_to_buffer(ImageRegion)` — no override (cmd-buffer-level exists as building block).
- **M-caps (MED):** `DeviceLimits` all-zero; `DeviceFeatures` only sets `ray_tracing`/`ray_query`.
- **M12 (MED):** `hello_metal` is a headless boot smoke — needs a windowed NSWindow/CAMetalLayer/draw/present host; also tightens `acquire_next_image` signal/fence + `swapchain_image` handle.

Intentionally-deferred on Metal: SBT pipeline tier, `begin_parallel_render_pass` (nullptr fallback), `debug_group_depth` (test-only). `MetalDevice.cpp` Apple-branch stub is dead text when the `.mm` is built.

## 2. Impl-Now Checklist (the ONLY work before "backend finished")

| ID | Backend | Item | Primary file(s) | Effort | Win-autonomous |
|----|---------|------|-----------------|--------|----------------|
| D3 | D3D12 | Set 8 unset DeviceFeatures flags from `CheckFeatureSupport` | `D3D12Device.cpp:357-405` | 0.5d | yes |
| D1 | D3D12 | Honor MSAA `SampleCount` (texture + graphics/mesh PSO) | `D3D12Device.cpp:660,1807-1808,1658` | 0.5d | yes |
| D4 | D3D12 | Branch `kStorageImage` UAV ViewDimension (3D/2DArray/1D) | `D3D12Device.cpp:2767-2774` | 0.5d | yes |
| D2 | D3D12 | Shader-visible sampler heap + `kSampler` write + sampler root table | `D3D12Device.cpp:1071-1080,2855-2862,4569-4582,4974` | 1.5d | yes |
| M6 | Metal | Real threadgroup size from SPIRV-Cross reflection | `MetalDevice.mm:873-914`, `MetalInternal.hpp`, `MetalCommandBuffer.mm:854` | 1d | no |
| M-caps | Metal | Fill `DeviceLimits` + advertise mesh/bindless/aniso features | `MetalDevice.mm:420-434` | 0.5d | no |
| M10 | Metal | `create_mesh_pipeline` + `draw_mesh_tasks` | `MetalDevice.mm`, `MetalCommandBuffer.mm` | 1d | no |
| M11 | Metal | bindless array create/write/destroy + bind | `MetalDevice.mm`, `MetalCommandBuffer.mm` | 1.5d | no |
| M-readback | Metal | device-level `copy_image_to_buffer(ImageRegion)` readback | `MetalDevice.mm` (reuse `MetalCommandBuffer.mm:696-740`) | 0.5d | no |
| M12 | Metal | windowed `hello_metal` host + acquire signal/swapchain_image | `samples/rhi/hello_metal/main.cpp`, `MetalDevice.mm:1500-1521,1576-1584` | 1.5d | no |

**Total impl-now: 0 Vulkan + 4 D3D12 + 7 Metal = 11 items, ~9 engineering-days.**

## 3. Execution Order

All build-heavy strands share ONE `build/ninja-debug` dir. Metal `.mm` is gated-OFF on Windows (no link contention) but its test/sample CMake edits touch shared `CMakeLists` — serialize those one at a time.

1. **D3D12 cluster (fully Windows-verifiable, ~3d):** D3 -> D1 -> D4 -> D2. `cmake --build --preset ninja-debug` + `ctest --preset ninja-debug --output-on-failure` after each.
2. **Metal cluster (write-now, Mac-verify-later, ~6d):** M6 -> M-caps(limits) -> M10 -> M11 -> M-caps(advertise after M10/M11) -> M-readback -> M12. Build gated-off after each; apply test/sample `CMakeLists` edits serially.
3. **Tests + Mac-GPU verify (the residue):** D3D12 parity tests run on Windows now; Metal gtests + golden readback run on a Mac in the M0 build-gate pass.

## 4. DONE Criteria — when is backend FINISHED FOREVER

Backend implementation is declared complete and we move to features (X5, DDGI) when ALL of:

1. **All 11 impl-now items merged**, each with a clean `cmake --build --preset ninja-debug`.
2. **D3D12 parity tests green on Windows:** MSAA render-target + custom-sampler-filtering (POINT/REPEAT/comparison) + the 8 feature flags reported true + 3D-storage-image UAV correctness.
3. **Metal code structurally reviewed + build-gated-off-clean on Windows**, and (HW-gated, deferrable to a Mac) **Mac-GPU verified:** `CD_RHI_METAL_ENABLED=ON` build + ctest green + windowed `hello_metal` runs + golden readback matches for compute (M6), mesh (M10), bindless (M11), readback (M-readback).
4. The remaining residue is EXACTLY the allowed set: tests, Mac-GPU-verify, and the documented intentional-defers (SBT tier, AS compaction/refit, Metal parallel-pass, D3D12 structured-buffer/StoreOp, debug_group_depth) + out-of-scope (indirect draw, OpenGL, Slang). **None of these counts as unfinished backend.**

## 5. User-Override Note (decisions, not gaps)

- **SBT RT-pipeline tier** (`create_rt_pipeline`/`dispatch_rays`) — CORRECTED 2026-06-16: this is a **real implementation on Vulkan AND D3D12**, not a stub. Vulkan `create_rt_pipeline` is a full `vkCreateRayTracingPipelinesKHR` body (`engine/render/rhi/src/vulkan/VulkanDevice.cpp:4351`; `dispatch_rays` → `vkCmdTraceRaysKHR`, `VulkanCommandBuffer.cpp:815`); D3D12 is a full `CreateStateObject` RTPSO (`D3D12Device.cpp:2396`; `dispatch_rays` → `DispatchRays`, `:6248`). Both are exercised end-to-end by `samples/rhi/hello_path_trace`. SBT-pipeline is absent **only on Metal** (falls to the `IDevice.hpp:409` base default), which is CLOSED as out-of-charter by ADR-20260616-backend-wontfix-decisions §B-RT-SCOPE. Inline **ray-query** is the chosen production RT path (ADR-20260615) and is present on all three backends. *(The earlier text here — "deliberately kNotImplemented on all three backends" — was FALSE and is retracted.)*
- Vulkan **AS compaction/refit** is V3-deferred (no RHI surface to request it yet). Promote on request.