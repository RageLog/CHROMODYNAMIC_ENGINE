# ADR-20260616: Backend wont-fix decisions (consolidated CLOSED record)

**Date**: 2026-06-16
**Status**: Accepted — all sections CLOSED (Backend-to-100 Wave 0b, GROUP B)
**Stakeholders**: RHI (Vulkan / D3D12 / Metal), Rendering, CI
**Format**: Iglberger (Context / Decision / Rationale / Promote-on-request trigger), one section per decision.
**Related**: `docs/ROADMAP_BACKEND_TO_100.md` (GROUP B), `docs/RHI_KNOTIMPL_INVENTORY.md`
(the single-source-of-truth kNotImplemented table referenced throughout), ADR-20260615
(ray-query is the chosen production RT path), ADR-20260529-X6 (Vulkan + D3D12 RT pipeline status).

> **Why one file:** these nine items are each a one-paragraph CLOSED decision. A single
> consolidated record is cleaner than nine tiny files and keeps the cross-references (all roads
> lead to the inventory + the roadmap) in one place. After this ADR each item is CLOSED, not open.
>
> **Load-bearing correction baked into §B-RT-SCOPE:** the SBT RT-pipeline path is a **real
> implementation** on Vulkan (`VulkanDevice.cpp:4351` → `vkCreateRayTracingPipelinesKHR`) and
> D3D12 (`D3D12Device.cpp:2396` → `CreateStateObject`), exercised by
> `samples/rhi/hello_path_trace`. This ADR must NEVER repeat the false "Vulkan create_rt_pipeline
> is a stub" / "SBT is kNotImplemented on all three backends" claim. SBT is absent ONLY on Metal.

---

## §B-RT-SCOPE — Metal SBT RT-pipeline is out of the parity bar

### Context
Two RT execution models exist: (1) **inline ray-query** (`rayQueryEXT` / DXR-1.1
`RayQuery` / `metal::raytracing::intersector<>`) and (2) the **SBT RT-pipeline**
(`vkCmdTraceRaysKHR` / `DispatchRays` with a shader binding table). Inline ray-query is the
engine's chosen production RT path (ADR-20260615) and is implemented on all three backends. The
SBT RT-pipeline is an EXTRA capability: a full real implementation on Vulkan
(`VulkanDevice.cpp:4351`, `vkCreateRayTracingPipelinesKHR`; `dispatch_rays` →
`VulkanCommandBuffer.cpp:815`) and D3D12 (`D3D12Device.cpp:2396`, `CreateStateObject`;
`dispatch_rays` → `D3D12Device.cpp:6248`), consumed by `samples/rhi/hello_path_trace`. Metal does
not override `create_rt_pipeline` / `dispatch_rays`, so they fall to the `IDevice` base default
(`IDevice.hpp:409` → `kNotImplemented`).

### Decision
**CLOSE** Metal SBT RT-pipeline as out-of-charter; do NOT implement it. The production RT path
(inline ray-query) is fully present on Metal (AS build + `intersector<>` lowered by SPIRV-Cross).
SBT RT-pipeline remains an extra that two of three backends carry for `hello_path_trace`.

### Rationale
- **Parity-safety:** the engine never depends on SBT-pipeline RT in any render path — every shipped
  RT effect (chrome reflection, DDGI, ReSTIR) uses inline ray-query, which Metal HAS. No engine
  capability regresses on Metal by leaving SBT absent.
- Implementing Metal SBT would be an XL effort (visible-function-table +
  intersection-function-table + SBT authoring) for a path with zero engine consumers.
- The asymmetry is intentional and now documented, not an accidental hole.

### Promote-on-request trigger
A render feature that genuinely requires the SBT-pipeline model (e.g. a callable-shader-heavy
path-tracer) and must run on Metal. Then M-RT-PIPELINE becomes a scoped epic.

---

## §B-OPENGL — OpenGL backend permanently out-of-charter

### Context
`engine/render/rhi/src/opengl/OpenGLDevice.cpp` carries 16 `kNotImplemented` returns and is ~25%
complete. The engine charter is **Vulkan → D3D12 → Metal**.

### Decision
**CLOSE** the OpenGL backend as permanently out-of-charter. Stop counting it as an "open" backend
in any completion metric or parity audit.

### Rationale
- **Parity-safety:** OpenGL is not on the three-backend bar; its kNotImplemented sites do not
  contribute to the "zero TODO-holes" claim (they are explicitly excluded in the inventory).
- Maintaining a fourth backend dilutes effort on the three charter backends with no product
  requirement behind it.

### Promote-on-request trigger
A shipping target platform that only exposes OpenGL (e.g. a legacy/embedded port). Then OpenGL
re-enters scope as a new charter decision, not a backfill.

---

## §B-D3D12-STOREOP — StoreOp / kDontCare is a benign perf hint on D3D12

### Context
Vulkan honours `StoreOp::kDontCare` (lets the driver discard attachment contents to save
bandwidth). D3D12's render-pass model always preserves attachment contents on this code path;
`kDontCare` is not consumed.

### Decision
**CLOSE** as a perf-only hint with **no correctness impact**. D3D12 contents are always preserved;
a dropped `kDontCare` is strictly safe (preserve is the conservative behaviour).

### Rationale
- **Parity-safety:** correctness is identical across backends — D3D12 never reads undefined
  contents because it preserves them. Only a bandwidth optimisation is forgone.
- The prior doc word "symmetric" was imprecise; the precise statement is "Vulkan honours it, D3D12
  ignores it (benign — contents preserved)."

### Promote-on-request trigger
A measured render-pass bandwidth bottleneck on D3D12 where `D3D12_RENDER_PASS_ENDING_ACCESS_TYPE_
DISCARD` would help. Then wire `kDontCare` → DISCARD.

---

## §B-D3D12-STRUCTURED — kStorageBuffer is raw / ByteAddress-only

### Context
D3D12 `kStorageBuffer` SRV/UAV (`D3D12Device.cpp:1503`, `:3039`) is bound as a raw
`ByteAddressBuffer` / `RWByteAddressBuffer`. A typed `StructuredBuffer<T>` path would need a
per-binding stride field that does not exist on the RHI descriptor surface.

### Decision
**CLOSE** as raw-only. Every engine SSBO is already authored as raw / ByteAddress on the HLSL side,
so this matches actual shader usage.

### Rationale
- **Parity-safety:** all engine SSBOs are raw; nothing currently consumes `StructuredBuffer<T>`, so
  there is no divergence in shipped shaders.
- Adding a stride field to the interface for an unused path is speculative API surface.

### Promote-on-request trigger
A shader that declares `StructuredBuffer<T>` / `RWStructuredBuffer<T>` and must run on D3D12. Then
add a stride field to the buffer-view descriptor + the typed SRV/UAV path.

---

## §B-D3D12-PARALLEL — sequential-replay parallel render pass is correctness + determinism 100%

### Context
D3D12 has no Vulkan-style inheriting secondary command lists. The D3D12
`begin_parallel_render_pass` (`D3D12Device.cpp:6318`+) records each lane into a thread-confined
deferred command list (true parallel RECORDING), then REPLAYS them onto the primary list **in lane
order** — byte-identical to a single thread recording the same draws.

### Decision
**CLOSE** the sequential-replay model as the D3D12 parallel-pass implementation. Correctness AND
determinism are 100%; only peak replay throughput differs from Vulkan secondaries.

### Rationale
- **Parity-safety:** the emitted command stream is byte-identical to a single-threaded lane-order
  recording (verified by the design contract at `D3D12Device.cpp:6329-6335`). Output pixels and
  command ordering match Vulkan exactly — the X1-FU-F parallel-recording scaling win is preserved.
- A true per-lane `ID3D12GraphicsCommandList` variant buys only the replay-throughput delta, which
  is not on the current bar.

### Promote-on-request trigger
A profile showing the single-threaded replay phase is a frame-time bottleneck on a real workload.
Then implement per-lane direct command lists with explicit state inheritance.

---

## §B-D3D12-SYNC — binary semaphores are a no-op on D3D12 (timeline/fence is the primitive)

### Context
D3D12's queue-side sync primitive is `ID3D12Fence` (effectively a Vulkan timeline semaphore;
`D3D12Device.cpp:3437`+). The acquire/submit paths treat Vulkan-style binary semaphores as no-ops
(`:6536`, `:6571`) and map `SemaphoreHandle` onto a fence + per-instance value.

### Decision
**CLOSE** with an explicit cross-backend sync contract: **timeline/fence is the real cross-queue
primitive on D3D12; binary semaphores are inert.** Callers needing cross-queue sync use timeline
values.

### Rationale
- **Parity-safety:** the model is internally consistent — every cross-queue dependency the engine
  expresses is satisfied via fence/timeline values, which D3D12 implements fully. No synchronisation
  is silently dropped because the engine does not rely on binary-semaphore semantics for D3D12.
- This makes the cross-backend sync expectation a documented contract, not an accident.

### Promote-on-request trigger
A cross-backend code path that genuinely needs binary-semaphore semantics on D3D12 (none today).
Then back binary semaphores with single-shot fences.

---

## §B-CAP-GATES — capability-gate kNotImplemented returns are designed feature-gate semantics

### Context
RT / mesh-shader / bindless `create_*` methods return `kNotImplemented` when the adapter lacks the
feature (e.g. `!features_.ray_tracing`, `!features_.mesh_shader`, `!features_.bindless_resources`).
The `kNotImplemented` code is defined precisely for this at `IDevice.hpp:93-96`: "feature
recognized but not yet implemented by this backend version… so callers can pick a sensible fallback
instead of treating it as a programming bug." Callers branch on `features()` first.

### Decision
**CLOSE** the capability-gate set as designed feature-gate semantics — NOT code holes. Each such
site has a real implementation body below the gate for adapters that DO support the feature. The
full enumeration lives in `docs/RHI_KNOTIMPL_INVENTORY.md`.

### Rationale
- **Parity-safety:** behaviour is identical and correct across backends — a capable adapter runs
  the real path; an incapable one gets a typed, gateable error that callers already check. This is
  the contracted negative path, not an unfinished feature.

### Promote-on-request trigger
None — this is the permanent contract. (If the gate string drifts from the actual `features()`
check, that is a doc-sync bug to fix in the inventory, not a promotion.)

---

## §B-SPARSE — sparse / tiled resources are a future virtual-texture RFC

### Context
Sparse / tiled resources (`vkQueueBindSparse`, D3D12 reserved/tiled resources, Metal sparse
textures) are not on the current RHI surface. They are the substrate for a future virtual-texture
/ mega-texture streaming system.

### Decision
**CLOSE** sparse/tiled resources as a future RFC item, not on the current parity bar.

### Rationale
- **Parity-safety:** no engine path requests sparse residency today; nothing regresses by deferring.
  Adding it now would be a large cross-backend surface (residency callbacks, page tables) with no
  consumer.

### Promote-on-request trigger
A virtual-texture / streaming-megatexture feature lands and needs page-level residency control.
Then open a dedicated RFC + ADR for the sparse-resource RHI surface.

---

## §B-NULL-NONBLOCK-READBACK — async/fenced non-blocking readback is deferred

### Context
A blocking one-shot image→buffer readback exists and is tested (the command-level `copy` is the
building block). An asynchronous / fenced non-blocking readback (submit copy, poll a fence, map
later without stalling) is not yet exposed.

### Decision
**CLOSE** async/fenced non-blocking readback as deferred. The blocking one-shot path covers current
needs (golden capture, debug readback).

### Rationale
- **Parity-safety:** the blocking path is correct and identical across backends; nothing in the
  engine requires non-blocking readback today. The async variant is a throughput convenience layered
  on the same `copy_image_to_buffer` primitive that already exists.

### Promote-on-request trigger
A runtime feature that reads GPU results back every frame without a stall (e.g. GPU-driven
occlusion feedback, async screenshot pipeline). Then add a fenced readback handle + poll API on top
of the existing copy.
