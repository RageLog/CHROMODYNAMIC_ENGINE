# ADR-20260530 — Metal RHI Backend (cd::rhi::metal)

- **Status**: Proposed (Design-only, Phase 1 planning — NO IMPLEMENTATION)
- **Date**: 2026-05-30
- **Branch**: dev
- **Deciders**: Cemal TATLI
- **Author**: architect subagent
- **Related**: ADR-001 (RHI architecture), ADR-003 (Shader / SPIRV-Cross MSL), ADR-005 (Foundation policy), ADR-016 (Vendor matrix), ADR-20260529-X4 (D3D12 parity status), ADR-20260529-X6 (Vulkan RT pipeline status), ADR-20260529-X5 (shader on-disk hot reload)
- **Refines**: ADR-001 §"Açık Sorular" Q3 (Metal fallback scope — originally "out-of-scope v1, MoltenVK"). This ADR re-opens Q3 and replaces it with a **native Metal** primary path plus **MoltenVK fallback**.

> **Scope guard**: this document is a planning artifact. It commits to **no code**, **no CMake target wiring**, **no Objective-C / Swift / .mm sources**. The existing `engine/render/rhi/include/cd/rhi/metal/MetalDevice.hpp` stub header (Wave 97) remains the only Metal-side artifact in tree until a future ADR explicitly authorises Phase P1 implementation.

---

## 1. Bağlam (Context)

### 1.1 Why Metal, why now

`cd::rhi` today carries **four** backend stubs/implementations:

| Backend | Status (as of 2026-05-30) | Source root |
|---|---|---|
| Vulkan 1.3 | Production (primary) | `engine/render/rhi/src/vulkan/` |
| D3D12 | Parity sprint M4 in flight (~7 kNotImpl gaps) | `engine/render/rhi/src/d3d12/` |
| OpenGL 4.6 | Skeleton + triangle path | `engine/render/rhi/src/opengl/` |
| Metal | **Header stub only** — `create_metal_device()` returns `kBackendError` on non-Apple, no implementation on Apple either | `engine/render/rhi/include/cd/rhi/metal/MetalDevice.hpp` |

Without a Metal path the engine **cannot ship to any Apple platform** — macOS, iOS, iPadOS, visionOS, tvOS. Apple does not ship Vulkan on any of these and never will; the only first-party GPU API is Metal. App Store distribution further hardens this: Apple periodically rejects binaries that drive the GPU through unsupported abstraction layers when alternatives exist.

CHROMODYNAMIC's library-oriented charter (CLAUDE.md §7) and the "supersede SOTA" quality bar (memory bullet 4) demand a **first-class Metal backend**, not a transpiled emulation. Filament, bgfx, Diligent and The Forge all ship native Metal — the bar to clear.

### 1.2 Target platform matrix

| Platform | Minimum OS | Metal feature set | Notes |
|---|---|---|---|
| macOS (Apple Silicon) | macOS 14 Sonoma | Metal 3.0 — `apple7`+ family | M1/M2/M3/M4. Primary dev target. Unified memory; argument buffers tier 2 native. |
| macOS (Intel) | macOS 14 Sonoma | Metal 3.0 limited — `mac2` family | AMD Polaris/Vega/Navi only (no Intel iGPU acceleration). RT not available. Reduced feature set: no mesh shaders, no function pointers. Maintained as **best-effort** until Apple drops support (~2027 expected). |
| iOS | iOS 17 | Metal 3.0 — `apple7`+ (A14+) | iPhone 12 and later. Pre-A14 devices excluded by `iOS 17` floor. |
| iPadOS | iPadOS 17 | Metal 3.0 — `apple7`+ (M1 iPad Pro, A14 iPad Air+) | Same feature set as iOS. |
| visionOS | visionOS 1.0 | Metal 3.0 — `apple8`+ (M2) | Foveated rendering, stereo render pass mandatory. Driven via CompositorServices, **not** SwiftUI RealityKit (engine owns the frame). |
| tvOS | tvOS 17 | Metal 3.0 — `apple7`+ (A12 Apple TV 4K Gen 2) | Out of scope for engine v1; flagged for vendor matrix completeness. |

OS floor rationale: iOS 17 / macOS 14 align with Metal 3.0 universal availability, ResidencySet API, and `MTLBindingType::kPrimitiveAccelerationStructure` for raytracing without device-class gating per-call. Older OS support would require a parallel Metal-2 code path; that cost is rejected (§4 Reddedilen #5).

### 1.3 Metal 3.0 capability surface (what we can rely on)

| Feature | API surface | Mapping to `cd::rhi` |
|---|---|---|
| **Argument buffers tier 2** | `MTLArgumentBuffer` + `setArgumentBuffer:offset:` | Bindless descriptor heap analogue; closest peer to Vulkan `VK_EXT_descriptor_indexing`. |
| **Mesh shaders** | `MTLMeshRenderPipelineDescriptor` + `objectFunction` / `meshFunction` | One-to-one with Vulkan `VK_EXT_mesh_shader` (object→task, mesh→mesh). |
| **Raytracing** | `MTLAccelerationStructure` + intersection function tables | Direct map to Vulkan `VK_KHR_acceleration_structure` + `VK_KHR_ray_tracing_pipeline`. |
| **Function pointers (visible functions)** | `[[visible]]` MSL attribute + `MTLLinkedFunctions` | Vulkan `VK_KHR_ray_tracing_pipeline`'s callable shaders and SER analogue. |
| **ResidencySet (Metal 3.1, macOS 15)** | `MTLResidencySet` | Vulkan `vkQueueBindSparse` + `VK_KHR_memory_priority`'s pragmatic peer. Reduces page-in cost for large bindless heaps. |
| **Heap allocator (Metal 2.0+)** | `MTLHeap` | `cd::rhi::BufferHandle` / `TextureHandle` placed-resource backing. Tier 2 heaps required for argument-buffer offsets. |
| **Tile shading** | `tile` function stage on tile-based GPUs (Apple Silicon) | Optional fast-path for deferred / forward+ lighting; not exposed in `cd::rhi` v1 (forwarded to ADR-002 renderer follow-up). |
| **Programmable blending / pixel local storage** | Apple-only `[[color(n)]]` framebuffer fetch | Not exposed in `cd::rhi` v1. Renderer-side opt-in later. |

### 1.4 Two architectural forks

The engine has to choose **one** of:

- **Fork A — Native Metal**: own implementation, Objective-C++/.mm sources, MSL via SPIRV-Cross (already on disk).
- **Fork B — MoltenVK**: layer a single `cd::rhi::vulkan` instance on top of `libMoltenVK.dylib`, never touch Metal directly.

Fork B trades engineering scope for performance ceiling and feature coverage:

| Concern | Fork A (Native Metal) | Fork B (MoltenVK) |
|---|---|---|
| Engineering scope | High — full backend (~estimated 12-16 weeks for parity) | Low — recompile rhi_vulkan, add MoltenVK link | 
| Performance ceiling | Apple-tuned (tile shader, ResidencySet, argument-buffer tier 2 fast path) | Vulkan-trace + translation overhead; Khronos publishes ~3–8% loss on graphics-bound, more on compute-heavy |
| Metal 3.0 RT support | Native, ray-aligned semantics | MoltenVK gates this behind `MoltenVK 1.2.10+`; some extensions (function pointers, ray query) only partially mapped as of MoltenVK 1.3 |
| Mesh shaders | Native via `MTLMeshRenderPipelineDescriptor` | MoltenVK does **not** yet support `VK_EXT_mesh_shader` (tracked but unimplemented as of 1.3) |
| App Store risk | None | Apple discourages translation layers; periodic friction in WWDC guidance |
| visionOS | First-class (CompositorServices ↔ Metal direct) | MoltenVK does not target visionOS at all today |
| Debugger tooling | Xcode GPU Frame Debugger, Metal System Trace native | Indirect — must trace through Vulkan → Metal layer |
| Bindless descriptor parity | Argument buffers tier 2 maps directly | MoltenVK emulates `VK_EXT_descriptor_indexing` with argument buffers under the hood; works but throughput-bound |
| Licence | n/a (engine code) | Apache 2.0 — vendor-matrix safe |
| Cross-talk with our Vulkan code | None | Bug surface duplication: any Vulkan validation issue is **also** a MoltenVK issue |

The decision (§2) is **Fork A primary, Fork B as opt-in fallback** — not exclusive. MoltenVK remains a valid path for headless CI, third-party plugins, and resource-constrained titles that don't need Metal 3.0 mesh/RT.

### 1.5 SPIRV-Cross infrastructure already in tree

`engine/render/spirv_cross_glue/include/cd/spirv_cross_glue/Translate.hpp` (read 2026-05-30) already exposes:

```cpp
enum class Target : std::uint8_t { kGlsl, kHlsl, kMsl };
TranslateResult translate(std::span<const std::uint32_t> spirv,
                          Target target,
                          std::uint32_t version = 0);
```

with MSL versioning baked in (`20200 = MSL 2.2`, `30000 = MSL 3.0`). This means the **shader-translation half** of the Metal backend is **already infrastructure-complete** — the missing pieces are runtime device/pipeline/command-buffer code, not shader cross-compilation.

This is the single biggest reason the design favours Fork A: the unique-to-Metal cost (MSL translation) has been pre-paid.

### 1.6 `cd::rhi::IDevice` contract — gaps Metal cannot fully fill on Day 1

`IDevice.hpp` (read 2026-05-30) exposes capabilities the engine treats as load-bearing. The following gaps will **need explicit handling** in the Metal backend, surfacing through either `rhi_errors::Code::kNotImplemented` (caller-recoverable) or `kBackendError` (hard fail):

| `cd::rhi::IDevice` capability | Metal status | Mitigation |
|---|---|---|
| Bindless descriptor indexing (Vulkan `VK_EXT_descriptor_indexing` semantics) | iOS < 17 / macOS < 13: not generally available; iOS 17 / macOS 14: **available** via argument buffers tier 2 | Floor at iOS 17 / macOS 14 (§1.2). Pre-floor devices report `kBackendInitFailed`. |
| Multi-queue (graphics + compute + transfer explicit) | Metal has **one** `MTLCommandQueue` family; parallel command buffers per queue but no separate transfer queue concept | Map all three engine queues onto a single `MTLCommandQueue` with `concurrentDispatchType` hint; `cd::rhi::QueueType::kTransfer` retains semantic value but is colocated. Document in capability struct. |
| Timeline semaphores (`TimelineSemaphoreHandle`) | `MTLSharedEvent` provides the equivalent (signal/wait by `uint64_t` value) | One-to-one. No gap. |
| Binary semaphores (cross-queue handoff) | `MTLEvent` for intra-device, `MTLSharedEvent` for cross-process | Map `SemaphoreHandle` → `MTLEvent`; document non-cross-process restriction. |
| Fence (CPU-visible completion) | `MTLCommandBuffer addCompletedHandler:` + `MTLSharedEvent listener` | Adapter shim; semantic gap is null. |
| Subpass dependency / input attachment | Metal `MTLRenderPassDescriptor` + storeAction `dontCare` + Apple-only programmable blending fetch | iOS/Apple Silicon: native (better than Vulkan). Intel Mac: emulate via render-pass split. Renderer-side concern; RHI exposes only the descriptor. |
| Acceleration structures + RT pipeline | macOS 14 Apple Silicon: full; Intel Mac: **none**; iOS 17 A14+: full | Gate behind `IDevice::caps().ray_tracing_supported`; never silently fall back. |
| Mesh shaders | macOS 14 Apple Silicon: full; Intel Mac: **none**; iOS 17 A14+: full | Gate behind `caps().mesh_shaders_supported`. |
| Function pointers (callable shaders) | Metal `[[visible]]` + linked functions: supported on `apple6+` and `mac2+` (subset) | Gate behind `caps().shader_function_pointers_supported`; MSL translation requires SPIRV-Cross 2023.04+ which we already vendor. |
| Sparse / tiled resources | iOS 17 / macOS 14: `MTLDevice supportsSparseTextures` (limited tier) | Gate behind `caps().sparse_textures_supported`; v1 ships **without** sparse support. |
| Multi-draw indirect with draw-count buffer | Metal: `drawIndexedPrimitives` indirect with command buffer (ICB); count buffer requires ICB encoder | Map via `MTLIndirectCommandBuffer`; ICB has tier limits documented in `MTLArgumentBuffersTier`. |
| Cooperative-matrix / WMMA | Not exposed in Metal Shading Language as of MSL 3.1 | `caps().cooperative_matrix = false`; ML/inference workloads route through MPSGraph instead (out of `cd::rhi` scope). |
| Vulkan-style explicit memory barriers | Metal uses `MTLBarrierScope` + automatic hazard tracking for non-heap resources | Auto-tracking is **default-on** for non-heap allocations; `BarrierMode::Manual` (ADR-001) maps to heap-backed resources + `useResource:usage:stages:`. |

**Calling out the contract-shape decision**: when a Metal device cannot fulfil an `IDevice` call (e.g. `create_mesh_pipeline()` on Intel Mac), it returns `kNotImplemented` rather than `kBackendError`. The engine's existing pattern (D3D12 M4 sprint, X4 status ADR) already requires callers to handle `kNotImplemented` as a recoverable signal.

### 1.7 SOTA reference points

| Engine / library | Metal posture | Lesson |
|---|---|---|
| Filament | Native Metal backend, MSL emitted by `matc` toolchain | Confirms native is right call for an A++ engine. |
| bgfx | Native Metal, additionally exposes MoltenVK as a separate renderer enum | Two-fork model has precedent. |
| The Forge | Native Metal, argument buffers tier 2 used as default heap | Argument-buffer-first is mainstream now. |
| Diligent Engine | Native Metal, MoltenVK abandoned | Convergence on native. |
| Unreal 5 | Native Metal (RHI), mesh shaders supported on Apple Silicon | First-tier reference. |
| Dawn / WebGPU | Native Metal | Constraint-language argument-buffer mapping influenced our model. |
| Unity 6 | Native Metal + Vulkan layer abandoned circa 2022 | Same convergence story. |
| Godot 4 | Native Metal landed 4.3 (2024) replacing earlier OpenGL ES path | Open-source confirmation point. |

No production engine relies on MoltenVK as the **sole** Apple path; it is universally either dropped or held as a fallback. CHROMODYNAMIC follows the consensus.

### 1.8 Constraints

- C++23 baseline, `.mm` Objective-C++ files quarantined to `engine/render/rhi/src/metal/` only; no `__OBJC__` leakage into public headers.
- Compiler matrix: Apple clang (Xcode 15.4+); MSVC/GCC see only the stub header (`MetalDevice.hpp` already platform-gated, Wave 97).
- Vendor policy (ADR-016): Apple frameworks are **not** vendored — they are system libraries linked dynamically. No vcpkg port needed.
- Licence: no third-party redistribution beyond MoltenVK (Apache 2.0) when the MoltenVK fallback is built.
- Build presets: a new `mac-arm64-debug` / `mac-arm64-release` CMake preset family lands with P1 (out of this ADR's scope; flagged for the implementation PR).
- CI: GitHub Actions `macos-14-arm64` runner suffices for headless build + unit tests; rendering golden-image tests require a self-hosted Mac mini M2 (X3 NVIDIA CI ADR precedent applies).

---

## 2. Karar (Decision)

CHROMODYNAMIC adopts a **two-track Metal strategy**:

### 2.1 Primary: Native Metal backend (`cd::rhi::metal::MetalDevice`)

- Implements `cd::rhi::IDevice` and `cd::rhi::ICommandBuffer` in **Objective-C++ (`.mm`)** behind the existing `engine/render/rhi/include/cd/rhi/metal/MetalDevice.hpp` factory.
- Argument-buffer-tier-2 model is the **canonical descriptor binding**; the engine's bindless table is laid out as one root argument buffer per descriptor space (matching the renderer's existing root-signature shape).
- MSL shaders generated **offline** by `cd::spirv_cross_glue::translate(..., Target::kMsl, 30000)` during cook (ADR-006 asset pipeline) and **at runtime** for hot-reload (ADR-20260529-X5). Both paths share the same translation glue — no fork.
- Pipeline cache keyed by the existing `cd::rhi::PipelineCacheKey` (no Metal-specific key needed; MSL source is one input among others).
- Single `MTLCommandQueue`; queue family multiplexing exposed at the engine layer with documented "single-queue" capability bit.
- One `MTLHeap` per resource lifetime class (transient / per-frame / static / staging). Heap-backed allocations are mandatory for argument-buffer-tier-2 references.
- Validation layer = `MTL_DEBUG_LAYER=1` toggle wired via `MetalCreateInfo::enable_validation`.

### 2.2 Secondary: MoltenVK fallback (`cd::rhi::vulkan_moltenvk`)

- **No new code** beyond a CMake option `CD_RHI_VULKAN_MOLTENVK=ON` that:
  - links `libMoltenVK.dylib` (or static `MoltenVK.xcframework`) when targeting macOS/iOS,
  - emits a `Backend::kVulkan` device through the existing `rhi_vulkan` library,
  - is selected only when `CD_RHI_METAL=OFF` or explicitly opted in via `create_native_device(NativeBackend::kVulkanMoltenVK)`.
- Intent: keep the door open for (a) developers who already have a Vulkan asset/shader pipeline and need a turn-key Mac build, (b) headless CI on Apple runners where Metal validation isn't worth wiring up, (c) third-party plugins that lock to Vulkan symbols.
- **Not** the shipping path for first-party titles. The vendor matrix marks it as `kReplaceWhenPossible` — once Metal native is feature-complete (P3), MoltenVK is downgraded to "supported but not recommended".

### 2.3 Capability advertisement

`IDevice::caps()` extended with Metal-specific bits (all booleans default false on non-Metal backends):

```cpp
// CONTRACT ONLY — no implementation.
struct DeviceCaps {
    // ... existing bits ...
    bool argument_buffers_tier2 = false;     // Metal: required, gates bindless
    bool tile_shading = false;               // Metal Apple Silicon only
    bool mesh_shaders_supported = false;     // already present, set on Apple Silicon
    bool ray_tracing_supported = false;      // already present, set on Apple Silicon
    bool shader_function_pointers_supported = false;
    bool residency_set_supported = false;    // Metal 3.1 / macOS 15
};
```

The renderer (ADR-002) and material/shader systems (ADR-003) consume `caps()` to pick a code path; this ADR does not change consumer semantics.

### 2.4 Phased delivery

| Phase | Scope | Acceptance criterion |
|---|---|---|
| **P0** (existing) | Stub header `MetalDevice.hpp` ships, factory returns `kBackendError`. No CMake target. | Already in tree (Wave 97). |
| **P1 — Basic raster + texture** | `MTLDevice` + `MTLCommandQueue` + `MTLLibrary` + heap allocator + argument-buffer tier-2 layout + SPIRV→MSL pipeline + minimal `ICommandBuffer` (begin/end render pass, set pipeline, set argument buffer, draw / drawIndexed, present via `CAMetalLayer`). | `Project/HelloEngine` sample boots on macOS 14 / Apple Silicon, draws Sponza atrium with baseColor textures, 60 Hz, no validation errors. Golden-image diff vs Vulkan reference within ΔE ≤ 1.5. |
| **P2 — Raytracing** | `MTLAccelerationStructure` BLAS/TLAS build + ray-query intersection in pipeline state + RT reflection demo + function-pointer-based hit shaders. | `Project/HelloEngine` RT reflection toggle reaches feature parity with Vulkan path (X6 ADR baseline). iOS 17 A14+ device passes the same suite. |
| **P3 — Mesh shaders + tile shading** | `MTLMeshRenderPipelineDescriptor` end-to-end; engine's existing mesh-shader path lights up on Metal. Optional: tile-shader fast-path for Apple Silicon tile-based deferred. | Mesh-shader Sponza variant matches Vulkan output. Tile-shader path gated behind a renderer opt-in flag; not blocking. |
| **P4 (out-of-ADR)** | visionOS CompositorServices integration, foveated rendering, stereo render pass. Sparse textures. ResidencySet. Cooperative matrix (if MSL exposes it). | Tracked separately; a successor ADR locks the design when P3 is signed off. |

Hard rule: **no phase ships without a golden-image regression test** added to the existing parity matrix (ADR-20260529-X4 / X6 precedent).

---

## 3. Reddedilen Alternatifler

| # | Alternative | Reason rejected |
|---|---|---|
| 1 | **MoltenVK only** (skip native Metal) | Mesh shaders, visionOS, RT function pointers either unsupported or fragile. Performance ceiling 3–8% below native on graphics, worse on compute. Apple App Store posture risk. No production AAA engine relies on this path solely. |
| 2 | **Native Metal only**, drop MoltenVK entirely | Loses turn-key entry for Vulkan-pipeline-only users and headless CI bring-up. Cost of keeping it (one CMake option, one factory branch) is negligible. |
| 3 | **MetalCpp (Apple's C++ header bindings)** as primary | MetalCpp lags Metal release by ~1–2 OS versions, omits some entry points (notably ResidencySet on early releases), and forces a heavier `.cpp` dialect choice that complicates `.mm` interop. Adopting `.mm` directly is simpler and forward-compatible. Re-evaluate at P3. |
| 4 | **Slang-emitted MSL** (instead of SPIRV-Cross) | Slang's MSL emitter is excellent, but adopting Slang as primary shader frontend is an ADR-003-scale decision and out of band. SPIRV-Cross MSL is already vendored, already exercised by the M4/D3D12 work, and already in `engine/render/spirv_cross_glue/`. Re-evaluate when ADR-003 revisits Slang. |
| 5 | **Support Metal 2.x** to widen device coverage | Argument-buffer tier 2, mesh shaders, RT, function pointers all require Metal 3.0 paths anyway. A dual Metal 2.x / 3.0 code path doubles the surface for ~3 years of legacy iPhones. Rejected on engineering-cost grounds. |
| 6 | **CompositorServices-first visionOS path before P3** | visionOS is a v1.0 SKU with rapidly moving APIs. Foundation must be solid first; visionOS lands in a successor ADR with its own design pass. |
| 7 | **One MTLCommandQueue per engine queue type** (graphics/compute/transfer) | Apple's documented guidance is one queue per device for nearly all workloads; multiple queues add scheduling cost and rarely help on tile-based GPUs. Single queue + parallel command buffers is the SOTA shape. |
| 8 | **Direct `.metal` shader authoring** (skip SPIRV→MSL) | Splits shader source from Vulkan/D3D12. Authoring once in GLSL/HLSL and translating is mandatory for cross-API parity tests. |
| 9 | **Defer Metal until Phase 2** | Apple platforms are a flagship distribution tier. Phase 2's foundation closure (ADR-018) already assumed Metal would be reached; deferral pushes a multi-month gap. |
| 10 | **Skip explicit `caps()` advertisement, throw on unsupported features** | Violates ADR-001's error-policy contract (`std::expected` everywhere, no exception escape). Capability bits + `kNotImplemented` is the established pattern. |

---

## 4. Sonuçlar (Consequences)

### 4.1 Pozitif

- Apple platform tier unlocks: macOS, iOS, iPadOS, visionOS reachable from one engine codebase.
- Native MSL via SPIRV-Cross reuses **existing** infrastructure — zero new shader-toolchain code.
- Argument-buffer-tier-2 design aligns with the engine's bindless-first descriptor model (ADR-001), so renderer-side code remains backend-agnostic.
- Two-track strategy (native + MoltenVK) gives integrators a soft on-ramp without compromising the first-party path.
- `caps()`-gated feature advertisement preserves cross-platform safety: callers always know what to expect before calling.
- visionOS roadmap is real — not a deferred dream — because P3 leaves a clean handoff to a CompositorServices ADR.

### 4.2 Negatif / Risk

- Apple-only build dialect (`.mm`, Apple clang, Xcode toolchain) adds CI complexity. Mitigation: GitHub Actions `macos-14` runner + (later) self-hosted Mac mini M2 for golden-image tests.
- Mesh-shader and RT availability is **device-dependent**: Intel Macs are second-class. Mitigation: explicit `caps()` gating; rendering pipeline already has fallbacks designed in (ADR-002).
- Single-queue model means our `QueueType::kTransfer` is semantic-only on Metal. Mitigation: documented capability; engine code already tolerates queue colocation.
- MoltenVK fallback drift: as Metal evolves, MoltenVK's coverage will lag for 6–18 months per feature. Mitigation: vendor matrix marks MoltenVK as `kReplaceWhenPossible`; CI tests against MoltenVK gated to known-good versions.
- Validation tooling: Metal validation layer is less verbose than Vulkan validation. Mitigation: add a structured RHI-call logger at P1 to compensate.
- iOS 17 / macOS 14 floor cuts off the iPhone 11 generation and older Macs. Mitigation: accepted, see §1.2 and Rejected #5.

### 4.3 Replace-Ready (ADR-016 disiplini)

- `cd::rhi::metal::*` — engine-owned, no replacement target. Apple frameworks (`Metal.framework`, `Foundation.framework`, `QuartzCore.framework`) are system libs, not vendored.
- `MoltenVK` — `kReplaceWhenPossible`; downgrade once native Metal is at P3.
- SPIRV-Cross — `kKeep` (already vendored, ADR-016 baseline).

---

## 5. Açık Sorular

| ID | Soru | Karar | Çözüm noktası |
|---|---|---|---|
| M-Q1 | MetalCpp vs `.mm` for backend implementation | `.mm` for v1; revisit at P3 | P3 acceptance review |
| M-Q2 | ResidencySet adoption — wait for macOS 15 floor, or feature-flag | Feature-flag behind `caps().residency_set_supported`, default off | Post-P1 perf pass |
| M-Q3 | visionOS — own ADR or extension of this one | Own ADR (P4 successor) | After P3 sign-off |
| M-Q4 | Intel Mac support window — drop with macOS 16? | Track Apple's deprecation; engine matches | Annual vendor-matrix review |
| M-Q5 | Tile-shader fast-path — renderer-side opt-in or RHI-side primitive | RHI exposes `tile_shading` cap only; renderer owns the path | ADR-002 follow-up |
| M-Q6 | Self-hosted Mac CI runner — Mac mini M2 vs M4 | Defer until P1 lands; spec at that gate | P1 acceptance |
| M-Q7 | MoltenVK static vs dynamic linking | Dynamic on macOS (system loader friendly), static on iOS (App Store constraint) | P1 CMake design |
| M-Q8 | Per-frame argument-buffer recycle strategy — ring vs slab | Ring of `kMaxFramesInFlight` argument buffers, slab inside | P1 prototype |

---

## 6. Cross-Cutting

- **ADR-001 (RHI)**: This ADR is the concrete Metal arm of ADR-001's backend roadmap. ADR-001 §"Açık Sorular" Q3 is hereby **closed** with the answer "native Metal primary, MoltenVK fallback" — superseding the earlier "out-of-scope v1" stance.
- **ADR-003 (Shader)**: MSL emission via `cd::spirv_cross_glue::translate(..., kMsl, 30000)` is already in tree. No new shader-pipeline ADR needed; this ADR confirms reuse.
- **ADR-005 (Foundation)**: `cd::core::Result` / `cd::core::ErrorCode` are the only error-surface types — no Apple-specific error class.
- **ADR-006 (Asset pipeline)**: Cooked MSL artifacts join the per-backend cook output (next to SPIR-V and DXIL).
- **ADR-014 (CI/CD)**: macOS runner integration is a follow-up; this ADR flags the requirement but does not specify the YAML.
- **ADR-016 (Vendor matrix)**: MoltenVK enters as `kReplaceWhenPossible`; Apple frameworks as system libs (not in matrix).
- **ADR-20260529-X4 (D3D12 parity status)**: precedent for `kNotImplemented` capability advertisement.
- **ADR-20260529-X5 (shader on-disk hot reload)**: runtime SPIRV→MSL re-translation reuses the same on-disk hot-reload contract.
- **ADR-20260529-X6 (Vulkan RT pipeline)**: Metal RT (P2) measures parity against this baseline.

---

## 8. P1 Implementation Task DAG (Run 22 follow-on planning)

**Status as of 2026-06-07 (Run 22 partial)**: ADR remains in
"Proposed (Design-only)" status — the §2 decision stands, the
§2.4 Phased delivery acceptance criteria are intact, but no P1
code has been written yet. The Vulkan path (`cd_rhi_vulkan`) is
production and the D3D12 path (`cd_rhi_d3d12`) is parity-driven
to ~3 kNotImpl gaps. Metal is the last platform fork to light.

This section breaks P1's "Basic raster + texture" acceptance into
concrete sub-tasks suitable for a future implementation session.
Each task is sized for one focused work-block; the dependencies
form the DAG below.

### 8.1 P1 sub-task list (file-by-file)

| ID | Sub-task | New file(s) | Edits | Acceptance |
|---|---|---|---|---|
| M1.1 | CMake target `cd_rhi_metal` (Apple-only, gated on `CMAKE_SYSTEM_NAME STREQUAL Darwin` or iOS toolchain). Wired into the same TARGET_OBJECTS/INTERFACE the existing Vulkan / D3D12 backends use. | `engine/render/rhi/CMakeLists.txt` patch | none | `cmake --build --preset xcode-debug` succeeds on macOS host; non-Apple builds skip the target with no warning. |
| M1.2 | `MetalDevice.mm` (Objective-C++) implementing `cd::rhi::IDevice` factory. Acquires `MTLDevice` (`MTLCreateSystemDefaultDevice()`), `MTLCommandQueue`, one `MTLLibrary` slot. Capability advertisement matches §2.3. | `engine/render/rhi/src/metal/MetalDevice.mm`, `.hpp` (small public header is already in tree as Wave 97 stub — keep it, fill the `.mm`) | `engine/render/rhi/include/cd/rhi/metal/MetalDevice.hpp` (replace the `kBackendError` factory body with real construction) | `cd::rhi::create_metal_device()` returns a usable `IDevice` on macOS 14+; old `kBackendError` path retained on non-Apple via `#if !__APPLE__`. |
| M1.3 | SPIRV→MSL translation glue inline call site. Reuse `engine/render/spirv_cross_glue` API. Output cached per (hash, target_family). | `engine/render/rhi/src/metal/MetalShader.mm` | none | First SPIRV blob handed to `create_shader_module` returns a valid `MTLLibrary` + function reference; second call hits the cache. |
| M1.4 | `MTLHeap` allocator for buffer + texture handles. Tier-2 placed-resource layout. | `engine/render/rhi/src/metal/MetalHeap.mm` | none | `create_buffer` / `create_texture` for the existing IDevice contract round-trip through the heap; no naked `MTLBuffer` / `MTLTexture` allocations in the backend. |
| M1.5 | Argument-buffer tier-2 descriptor binding. Maps `cd::rhi::DescriptorSetLayout` → `MTLArgumentBuffer` slot layout 1:1. | `engine/render/rhi/src/metal/MetalDescriptor.mm` | none | A `DescriptorSet` allocated via the existing IDevice contract resolves to one `MTLArgumentBuffer`; `bind_descriptor_set` writes the buffer's address into the command encoder slot. |
| M1.6 | Minimal `MetalCommandBuffer` implementing `begin_render_pass`, `bind_pipeline`, `bind_descriptor_set`, `bind_vertex_buffer`, `bind_index_buffer`, `draw_indexed`, `end_render_pass`. | `engine/render/rhi/src/metal/MetalCommandBuffer.mm` | none | Renders one full-screen quad with a texture; capture via Xcode GPU Frame Debugger shows the expected output. |
| M1.7 | Swapchain via `CAMetalLayer`. `present()` hooked to `MTLDrawable`. | `engine/render/rhi/src/metal/MetalSwapchain.mm` | needs a tiny `Project/HelloEngine` glue patch to feed the `CAMetalLayer` from the AppKit/UIKit window | A black window opens on macOS 14, swapchain rotates 3 frames per second under `present()` polling. |
| M1.8 | Wire `Project/HelloEngine` boot to select `create_metal_device()` on Apple platforms via the existing backend selection switch. | none | `Project/HelloEngine/main.cpp` factory selector | hello_engine launches on macOS 14 / M1 with no validation errors, draws floor mesh. |
| M1.9 | Sponza glTF load → Metal pipeline → first textured render. End-to-end smoke. | new test `tests/metal/test_hello_engine_metal_sponza.mm` | none | Capture matches Vulkan reference within ΔE ≤ 1.5 on the equivalent fixture. |

### 8.2 Dependency DAG

```
M1.1 ────┬─→ M1.2 ─→ M1.3 ─┬─→ M1.4 ─→ M1.5 ─→ M1.6 ─→ M1.7 ─→ M1.8 ─→ M1.9 ✓
         │                 │
         │                 └─→ tests/metal/test_metal_shader_translate.mm  (M1.3 unit cover)
         │
         └─→ tests/metal/test_metal_device_capability.mm  (M1.2 unit cover)
```

Two unit-coverage tasks fan off the longer hot path:
- **U1**: `test_metal_device_capability.mm` (gated on M1.2). Confirms the §2.3 capability advertisement matches macOS 14 / Apple Silicon's actual feature set.
- **U2**: `test_metal_shader_translate.mm` (gated on M1.3). Round-trips a fixed SPIRV blob through SPIRV-Cross and locks the MSL output against a checked-in golden string.

The integration golden (M1.9) is the §2.4 P1 acceptance gate.

### 8.3 Effort sizing (calendar weeks)

Per §1.4 Fork A estimate (12-16 weeks for full parity), P1 itself
sizes at **3-4 weeks of focused work** for a single engineer with
prior Vulkan / D3D12 backend experience. Parallel paths:

| Track | Tasks | Approx weeks |
|---|---|---|
| Build + device + shader (M1.1 .. M1.3 + U1 + U2) | 5 | 1.0 |
| Heap + descriptor + command (M1.4 .. M1.6) | 3 | 1.0 |
| Swapchain + boot + smoke (M1.7 .. M1.9) | 3 | 1.5 |
| Buffer / contingency | — | 0.5 |

### 8.4 Pre-flight checklist (before opening Phase P1)

- [ ] Confirm Xcode 15.x toolchain available on the dev macOS host.
- [ ] Confirm `vcpkg` triplet `x64-osx` or `arm64-osx` builds the existing manifest without errors.
- [ ] Confirm the SPIRV-Cross MSL backend in `engine/render/spirv_cross_glue` round-trips one of the existing prim-material SPIRV blobs to clean MSL (no `// unimplemented` markers in the output).
- [ ] Confirm the Vulkan reference golden capture for the chosen P1 acceptance fixture exists at the canonical path and is byte-stable across two `cmake --build` runs.
- [ ] **User sign-off** that the §2 decision still holds: Fork A (native Metal) as primary, Fork B (MoltenVK) as opt-in fallback. The fork choice is the most expensive thing to change later.

When all five boxes tick, dispatch a `developer` subagent on M1.1
and follow the §8.2 DAG.

---

## 7. Kanıt (Evidence)

- Apple Metal Feature Set Tables — https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf (acc 2026-05-30)
- Metal Shading Language Specification 3.1 — https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf (acc 2026-05-30)
- MoltenVK runtime user guide — https://github.com/KhronosGroup/MoltenVK/blob/main/Docs/MoltenVK_Runtime_UserGuide.md (acc 2026-05-30)
- MoltenVK feature parity table — https://github.com/KhronosGroup/MoltenVK#vulkan-extension-support (acc 2026-05-30)
- Filament Metal backend — https://github.com/google/filament/tree/main/filament/backend/src/metal (acc 2026-05-30)
- bgfx Metal renderer — https://github.com/bkaradzic/bgfx/blob/master/src/renderer_mtl.mm (acc 2026-05-30)
- The Forge Metal renderer — https://github.com/ConfettiFX/The-Forge/tree/master/Common_3/Graphics/Metal (acc 2026-05-30)
- Apple CompositorServices (visionOS) — https://developer.apple.com/documentation/compositorservices (acc 2026-05-30)
- WWDC 2023 "Discover Metal mesh shaders" — https://developer.apple.com/videos/play/wwdc2023/10202/ (acc 2026-05-30)
- WWDC 2023 "Your guide to Metal raytracing" — https://developer.apple.com/videos/play/wwdc2023/10128/ (acc 2026-05-30)
- WWDC 2024 "Discover ResidencySet in Metal" — https://developer.apple.com/videos/play/wwdc2024/10199/ (acc 2026-05-30)
- SPIRV-Cross MSL backend — https://github.com/KhronosGroup/SPIRV-Cross/blob/main/spirv_msl.cpp (acc 2026-05-30)
- Engine-local SPIRV-Cross glue: `engine/render/spirv_cross_glue/include/cd/spirv_cross_glue/Translate.hpp` (read 2026-05-30)
- Engine-local Metal stub: `engine/render/rhi/include/cd/rhi/metal/MetalDevice.hpp` (read 2026-05-30, Wave 97)
- Engine-local IDevice contract: `engine/render/rhi/include/cd/rhi/IDevice.hpp` (read 2026-05-30)
