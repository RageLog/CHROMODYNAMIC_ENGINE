# CHROMODYNAMIC — RHI Backend Parity Roadmap (Vulkan → D3D12 → Metal)

> **Status**: Architect roadmap, 2026-06-14. **Scope**: design + ordered
> execution plan only. No `.cpp`/`.hpp` written from this doc — backend
> implementations land per the workflow ordering below.
> **Charter**: cross-platform / cross-API library-oriented hybrid 2D+3D
> engine; SOTA target = Filament / bgfx / The Forge / Diligent AŞILACAK;
> modern C++23; every library standalone-consumable.
> **User directive (2026-06-14)**: bring all three RHI backends to FULL
> parity in strict order — **Vulkan 100% complete FIRST**, then D3D12 to
> the Vulkan bar, then Metal to the Vulkan bar. Effort auto-tuned but
> maximal; agent-driven.
> **Authoritative references**: `engine/render/rhi/include/cd/rhi/IDevice.hpp`,
> `.../ICommandBuffer.hpp`, `docs/ADR/ADR-20260529-X4-d3d12-parity-status.md`,
> `docs/ROADMAP_PHASE_2.md` (§2 X4, §4 X1-FU-F), `docs/D3D12_PARITY_AUDIT.md`.

This roadmap converts six structured backend-audit reports (RHI contract,
Vulkan core + RT, D3D12, Metal, shader-cross, parity-tests) into three
ordered per-backend gap tables plus a single recommended execution
ordering. The **bar** is the Vulkan reference backend: every capability
Vulkan implements with a real body (not a runtime feature-gate) is a line
D3D12 and Metal must clear. Capabilities Vulkan itself lacks (indirect
draw, AS compaction, AS refit) are **explicitly out of scope** — they are
not part of the parity bar and are tracked separately as future SOTA work.

---

## §0. Reference-backend baseline (the bar)

The Vulkan backend (`engine/render/rhi/src/vulkan/`) is the de-facto
reference. The audit confirms **zero code-path stubs**: every
`IDevice`/`ICommandBuffer` pure-virtual has a real override, and all
seven remaining `kNotImplemented` returns are runtime feature-gates
(device lacks the extension), not TODO holes
(`VulkanDevice.cpp:1764,2749,3507,3881,4072,4246`). X1-FU-F parallel
render-pass (secondary command buffers) is fully implemented
(`VulkanCommandBuffer.cpp:900-1072`).

**Capabilities at the bar** (D3D12 + Metal must reach all of these):

| Domain | Capability | Vulkan evidence |
| ------ | ---------- | --------------- |
| Resources | buffer/texture/view/sampler create+destroy (VMA, cube/3D/array/MSAA/depth) | `VulkanDevice.cpp:902-1740` |
| Resources | texture upload (`copy_buffer_to_image`) + readback (device + cmd) | `VulkanDevice.cpp:3123-3345`, `VulkanCommandBuffer.cpp:580-613` |
| Pipelines | graphics PSO (full desc: vertex layout, blend, depth/stencil, MSAA) | `VulkanDevice.cpp:1387` |
| Pipelines | compute PSO + `dispatch` | `VulkanDevice.cpp:1687`, `VulkanCommandBuffer.cpp:486` |
| Pipelines | mesh-shader PSO + `draw_mesh_tasks` | `VulkanDevice.cpp:1758`, `VulkanCommandBuffer.cpp:726` |
| Descriptors | layout/alloc/update/bind + bindless (descriptor-indexing) | `VulkanDevice.cpp:2135-2329,3874-4061` |
| Sync | binary sema + fence + timeline sema (host wait/signal/peek) | `VulkanDevice.cpp:2424-2533` |
| Submit | full `SubmitDesc` (VkSubmitInfo2 / vkQueueSubmit2) | `VulkanDevice.cpp:3358-3487` |
| Swapchain | create/acquire/present/recreate + HDR10/scRGB negotiation | `VulkanDevice.cpp:2596-2940` |
| Barriers | `vkCmdPipelineBarrier2` buffer + image | `VulkanCommandBuffer.cpp:615-693` |
| RT | BLAS/TLAS create+build, RT PSO, SBT, `dispatch_rays`, AS barrier | `VulkanDevice.cpp:3501-4274`, `VulkanCommandBuffer.cpp:735-893` |
| RT | ray_query (inline, dominant production path) | `VulkanDevice.cpp:4336,4766`, descriptor write `:2273-2290` |
| Parallel | `begin_parallel_render_pass` + `IParallelPassRecorder` (X1-FU-F) | `VulkanCommandBuffer.cpp:900-1072` |
| Debug | push/pop debug group (VK_EXT_debug_utils) | `VulkanCommandBuffer.cpp:695-720` |

**Explicitly NOT in the bar** (Vulkan lacks them too — do not invent work
for D3D12/Metal here):

- Indirect draw / indirect dispatch — not in the `ICommandBuffer` surface
  at all (`BufferUsage::kIndirect` + `ResourceState::kIndirectArgument`
  are scaffolding only; no method exists). Engine-wide gap, separate RFC.
- GPU-side AS compaction (`VkQueryPool COMPACTED_SIZE` /
  `vkCmdCopyAccelerationStructureKHR`) — no Vulkan implementation; build
  flags are `PREFER_FAST_TRACE` only.
- In-place AS refit (`BUILD_MODE_UPDATE` / `ALLOW_UPDATE`) — Vulkan does a
  full rebuild every frame; perf limit, not a parity gap.
- Multi-queue async-compute / dedicated transfer queue — **Vulkan itself
  is single-queue** (`do_create_command_buffer` ignores `QueueType`,
  `VulkanDevice.cpp:3032`). This is a *shared* limitation; see §1 V4
  because closing it on Vulkan IS part of "Vulkan 100%".

---

## §1. Vulkan completion — close every Vulkan-side gap (PHASE A, FIRST)

Vulkan is ~95% complete. The audit surfaces a **small, bounded** set of
real gaps. Per the directive, Vulkan must be TAM bitmiş before any D3D12
or Metal work begins. X1-FU-F is already done, so it is NOT re-opened.

> **✅ STATUS (2026-06-14): VULKAN DECLARED TAM.** V1 DONE (phase1180 /
> `a531fd9`), V2 DONE (phase1181 / `27201fd`), pre-existing tidy debt
> cleaned (phase1182 / `a2aaed3`), V3 DEFERRED (perf-only, see below). The
> Vulkan correctness/feature bar is now FROZEN; D3D12 (Phase B) may begin.

| ID | Gap | Severity | Evidence | Effort | Blast radius |
| -- | --- | -------- | -------- | ------ | ------------ |
| **V1** | `barrier()` hardcodes `subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT` → depth/stencil texture transitions use the wrong subresource (validation error / silent no-op). Device-side readback already uses `aspect_for_format` (`VulkanDevice.cpp:3216`) — **asymmetric**. | **HIGH** (correctness) | `VulkanCommandBuffer.cpp:671` (also copy regions `:547,:591`) | **M** | command-buffer barrier path; needs texture-id→format map in `ResourceTables` |
| **V2** | Queue model: single universal graphics queue. `do_create_command_buffer(QueueType)` ignores the arg; submit/present always to `graphics_queue_`. No async-compute / dedicated transfer / separate present queue. | **MED** (SOTA target needs async-compute overlap; present-family rejection risk) | `VulkanDevice.cpp:3032,3482,2631,4692-4699,4602` | **L** | queue creation at factory + submit routing + `do_create_command_buffer` |
| **V3** | AS build flags hardcoded `PREFER_FAST_TRACE` with no `ALLOW_UPDATE`/`ALLOW_COMPACTION`. (Per-frame TLAS = full rebuild.) | LOW (perf, not correctness) | `VulkanDevice.cpp:3551,3753`, `VulkanCommandBuffer.cpp:783` | M | AS create + build paths |

**Decision — what "Vulkan 100%" means for this roadmap:**

- **V1 is mandatory** and is the *first* workflow (smallest blast radius,
  highest correctness value — it is a live correctness bug on any
  depth-attachment barrier). See §4.
- **V2 is mandatory for the directive** ("FULL parity", SOTA async-compute
  is a charter goal) but is **L** effort and structurally invasive. It is
  the second Vulkan workflow. Closing it on Vulkan *also* removes it from
  the shared-limitation list, which then *raises* the bar D3D12/Metal must
  clear — sequencing it inside Phase A (before D3D12) is correct so the
  bar is frozen once D3D12 starts.
- **V3 is OPTIONAL / deferred**: it is a perf limit Vulkan shares with no
  one (it is the reference), it requires changing `create_acceleration_structure`
  ABI expectations, and the user directive is about *parity*, not new SOTA
  RT features. Recommend a one-line ADR note deferring V3 to a future
  "RT performance" milestone rather than blocking Phase A on it.

**Ordered Vulkan work items:**

1. **V1 — depth-aware barrier aspect** (M). Add a texture-handle→`Format`
   lookup to `ResourceTables`; replace the hardcoded `COLOR_BIT` at
   `VulkanCommandBuffer.cpp:671` with `aspect_for_format(...)` (the helper
   already exists and is used device-side). Add a regression gtest that
   transitions a `D32_SFLOAT` texture and asserts no validation error.
2. **V2 — multi-queue support** (L). Query graphics/compute/transfer queue
   families; create dedicated queues when distinct; route
   `do_create_command_buffer(QueueType)` to the matching pool/queue;
   route `submit`/`present` to the correct queue; add a present-queue
   fallback when the graphics family cannot present. Stress test:
   async-compute submit overlapping a graphics submit via timeline sema.
3. **V3 — AS update flags** (M, *deferred — ADR note only unless user
   re-prioritizes*). If activated: thread `ALLOW_UPDATE` through
   `AccelStructureDesc`, add `MODE_UPDATE` refit path for skinned-mesh
   BLAS.

**✅ VULKAN DECLARED TAM (2026-06-14).**
- **V1 LANDED** (phase1180 / `a531fd9`): depth-aware barrier aspect across
  `barrier()` + both copy paths via an `image-id→Format` map in
  `ResourceTables`; the duplicate cmd-buffer aspect helper was deleted
  (single `vk_aspect_for_format` source so the unit test pins real wiring);
  a real regression net (atomic validation-error counter + assert) was added
  after adversarial review found the first tests passed on the buggy code.
- **V2 LANDED** (phase1181 / `27201fd`): `select_queue_families` (graphics/
  async-compute/dedicated-transfer/present with overlap-alias handling +
  family dedup), per-`QueueType` pool/queue routing for create/submit/present
  with graphics fallback. Graphics path byte-identical (golden), 5 existing
  graphics tests green, ownership-transfer barriers correctly scoped out
  (no shared cross-family in-flight work today).
- **V3 DEFERRED** (perf-only): AS build flags stay `PREFER_FAST_TRACE`
  (per-frame TLAS full rebuild). This is a perf refinement Vulkan shares with
  no one (it is the reference), needs an `AccelStructureDesc` ABI change
  (`ALLOW_UPDATE`/`ALLOW_COMPACTION` + a `MODE_UPDATE` refit path), and is
  outside the *parity* charter. Tracked for a future "RT performance"
  milestone; revisit if the user re-prioritizes.

Every checkpoint: build clean (WAE), chrome_probe golden byte-identical, new
gtests pass (validation-layer-gated cases honest-SKIP on this host, activate
in a layer-equipped CI). **The Vulkan bar is FROZEN for D3D12.**

---

## §2. D3D12 completion — bring to the Vulkan bar (PHASE B)

D3D12 (`engine/render/rhi/src/d3d12/`, ~4150 lines) is substantively done
on the **device** surface (queue/fence/swapchain, buffer/texture/view/
sampler, graphics/compute/mesh/RT PSO create, descriptors, binary+timeline
sema, `SubmitDesc`, AS create+build, SBT-handle fetch, device-level
readback). The stale `ADR-20260529-X4` lists 5 device `kNotImpl` sites as
QUEUED — **all 5 are now CLOSED**. The real gaps live on the
**command-buffer** side as silent base-class no-op inheritances, plus a
handful of partial PSO/render-pass translations.

| ID | Gap | Status | Severity | Evidence | Effort |
| -- | --- | ------ | -------- | -------- | ------ |
| **D1** | `copy_buffer_to_image` is an empty `{}` no-op → DEFAULT-heap textures can be created but **never receive pixel data**. Blocks ALL textured rendering on D3D12. | stub | **HIGH** | `D3D12Device.cpp:3944` | M |
| **D2** | `copy_image_to_buffer` (cmd-level, in-frame readback) empty `{}` no-op. Device-level one-shot variant IS real (`:2770`). | stub | MED | `D3D12Device.cpp:3945` | M |
| **D3** | `begin_render_pass` binds only `color_attachments.front()`, null DSV, `info.depth_stencil` ignored, MRT>1 dropped → no depth test, no G-buffer on D3D12. | partial | **HIGH** (3D) | `D3D12Device.cpp:3694-3726` | M |
| **D4** | Blend state hardcoded `BlendEnable=FALSE` for all 8 RTs; `desc.blend` ignored → all transparency/additive renders opaque. | partial | MED | `D3D12Device.cpp:1199-1216` | M |
| **D5** | Graphics PSO `DepthFunc` hardcoded `LESS`; `StencilEnable=FALSE` always; `desc.depth_stencil.compare` not translated. | partial | MED | `D3D12Device.cpp:1220-1227` | S |
| **D6** | `bind_vertex_buffer` stride hardcoded `24` → any vertex layout ≠ 24 B renders garbage. Must store per-binding stride in `GraphicsPipelineRecord`. | partial | MED | `D3D12Device.cpp:3786-3798` | M |
| **D7** | `dispatch_rays` not overridden → inherits base no-op. D3D12 can build AS + RTPSO + fetch SBT but **cannot trace rays** (silent no-op, no error). | missing | **HIGH** (RT) | `D3D12Device.cpp:3650-4079` (absent); base `ICommandBuffer.hpp:261` | M |
| **D8** | `bind_rt_pipeline` not overridden (prereq for D7). Needs `SetPipelineState1(state_object)`. | missing | MED | absent; base `ICommandBuffer.hpp:98` | S |
| **D9** | `acceleration_structure_barrier` not overridden (base no-op). `build_acceleration_structure` emits a per-build UAV barrier so partially mitigated. | missing | S | absent; base `ICommandBuffer.hpp:259` | S |
| **D10** | Bindless texture array (`create_bindless_texture_array`/`write_bindless_texture_slot`) not overridden; `features().bindless_resources` never advertised. SRV slot plumbing scaffolded in `update_descriptor_set` (`:2232-2291`) but array lifecycle + feature gate missing. | missing | MED-HIGH | `D3D12Device.cpp:2244` (comment); inherits kNotImpl | L |
| **D11** | `begin_parallel_render_pass` + `IParallelPassRecorder` lane mgmt + lane-order join. **DONE** (phase1191): overridden on `D3D12CommandBuffer`; `D3D12ParallelPassRecorder` holds N thread-confined `LaneRecorder`s (each an `IDrawRecorder` deferring its draw-subset into a per-lane command vector → parallel recording), `finish()` replays lanes onto the primary IN LANE ORDER with per-lane `rebind_parallel_pass_state` (RTV/DSV + viewport + scissor re-set, since D3D12 lists inherit no state). Sequential-replay-onto-primary fallback (D3D12 has no inheriting secondary lists / bundle forbids OMSetRenderTargets) → BYTE-IDENTICAL to serial. GPU correctness test `cd_test_d3d12_parallel_pass` (serial==parallel readback memcmp; tiled + overlap-last-write-wins lane-order guards). | done | MED | `D3D12Device.cpp` (override + recorder); `test_d3d12_parallel_pass.cpp` | XL |
| **D12** | DXIL shader toolchain (GLSL→SPIR-V→HLSL→DXIL) exists + tested but **NOT wired into `create_shader_module`**; `hello_d3d12_pbr` still uses hand-written inline SM5.1 HLSL. | missing wiring | **HIGH** | `D3D12ShaderToolchain.cpp:74-147` (built, unwired); `hello_d3d12_pbr/main.cpp:436-444` | M |
| **D13** | `DeviceLimits` default-zero (never filled from D3D12 caps). | stub | LOW | `D3D12Device.cpp:3428,287` | S |
| **D14** | `push_debug_group`/`pop_debug_group` empty `{}` → no PIX/RenderDoc grouping. | stub | LOW | `D3D12Device.cpp:4015-4016` | S |
| **D15** | `create_texture` has no initial-data path (consequence of D1). | missing | HIGH | `D3D12Device.cpp:376-493` | (folds into D1) |
| **D16** | Runtime GPU golden-parity test (D3D12 vs Vulkan pixels) absent; `test_backend_parity.cpp` is compile-time only. | missing | MED | `test_backend_parity.cpp`; ADR-X4:99 | XL (HW-gated) |

**Ordered D3D12 work items** (functional-correctness first, then RT, then
parallel, then verification):

1. **D12 — wire DXIL toolchain into `create_shader_module`** (M). The
   3-stage chain (`compose_glsl_to_dxil`) already produces valid DXIL with
   `cd::gluon` include resolution; the gap is plumbing it into the device
   path so the engine shader corpus flows through it instead of inline
   SM5.1 islands. Prerequisite for any meaningful golden parity.
2. **D1 + D15 — texture upload path** (M). `GetCopyableFootprints` +
   staging UPLOAD buffer + `CopyTextureRegion` + row-pitch alignment
   (inverse of the working device-level readback at `:2770`). Unblocks all
   textured rendering.
3. **D3 — depth + MRT in `begin_render_pass`** (M). DSV from
   `depth_stencil` view + `ClearDepthStencilView` +
   `OMSetRenderTargets` over all color attachments.
4. **D5 — PSO depth-compare + stencil** (S). Translate
   `desc.depth_stencil.compare`; couple with D3.
5. **D4 — per-attachment blend** (M). Translate blend factors/ops +
   `IndependentBlendEnable` for MRT.
6. **D6 — real vertex stride** (M). Store per-binding stride in
   `GraphicsPipelineRecord`, look up via bound pipeline.
7. **D2 — cmd-level `copy_image_to_buffer`** (M). Same `CopyTextureRegion`
   machinery as D1.
8. **D13 — populate `DeviceLimits`** (S) + **D14 — PIX debug markers**
   (S). Low-risk polish; can run in parallel with the above.
9. **D8 → D7 → D9 — DXR command path** (S→M→S). `bind_rt_pipeline`
   (`SetPipelineState1`), then `dispatch_rays`
   (`D3D12_DISPATCH_RAYS_DESC` from `DispatchRaysDesc` SBT regions), then
   `acceleration_structure_barrier` (UAV barrier). After this, D3D12 RT is
   functional end-to-end.
10. **D10 — bindless texture array** (L). Unbounded SRV descriptor-table
    range + `ResourceDescriptorHeap` (SM6.6) or large fixed table; light
    up `features().bindless_resources`.
11. **D11 — parallel render pass** (XL). Bundle / secondary command-list
    lane management + lane-order join, matching the Vulkan
    `IParallelPassRecorder` contract.
12. **D16 — GPU golden parity** (XL, HW-gated). `hello_d3d12_pbr` using the
    cross-compiled corpus + image-diff vs Vulkan. **Gated on NVIDIA
    self-hosted CI runner** (see §5).

---

## §3. Metal completion — build up to the Vulkan bar (PHASE C)

> **✅ STATUS (2026-06-15): METAL SOURCE SURFACE M1–M9 WRITTEN + REVIEWED;
> .mm Mac-COMPILE/GPU-test DEFERRED per user "donanım yok" directive.**
> M1 buffer/texture registry + M2/M8 pipeline & depth/MRT render-pass + M3
> SPIR-V→MSL toolchain (`MetalShaderToolchain`, **host-verified on Windows,
> 11/11 `cd_test_metal_shader_toolchain`**) + M4 argument-encoder descriptors +
> M5 barriers + M6 shader-module + M7 CAMetalLayer swapchain + M9 ray tracing
> (MTLAccelerationStructure BLAS/TLAS build + **ray-query MSL lowering
> host-verified**: SPIRV-Cross lowers `SPV_KHR_ray_query` → `intersection_query<>`,
> MSL 2.4 floor) — phases 1197–1202. The `.mm` translation units are gated
> behind `CD_RHI_METAL_ENABLED+APPLE` (OFF on Windows) so they are written +
> structurally reviewed but compile + run only on an Apple host. The remaining
> open items (M0 macOS build, M10 mesh-shader, M11 bindless-array, M12
> windowed `hello_metal`, and on-device GPU verification of M1–M9) are tracked
> in **`docs/METAL_MAC_TESTING.md`** (the on-Mac checklist) and
> **`docs/ADR/ADR-20260615-metal-backend-completion.md`** (the binding-model +
> RT decision record). Metal is NOT a blocker: yaz-ama-Mac-doğrulamasını-ertele.
> The gap table below is the ORIGINAL (2026-06-14) pre-work audit, kept for
> historical reference.

Metal is "skeleton + 5 sprint": broad source surface, **shallow functional
depth**, and — critically — **NEVER compiled or tested** on the current
Windows CI (`if(NOT APPLE)` FORCE-OFF, `engine/render/rhi/CMakeLists.txt:169`).
The "27/27 implemented" Sprint-5 claim is **unverified per CLAUDE.md §3
evidence rule** — it is source-reading only. Metal is the largest body of
work and is correctly last.

| ID | Gap | Status | Severity | Evidence | Effort |
| -- | --- | ------ | -------- | -------- | ------ |
| **M0** | macOS toolchain + `.mm` compile + `hello_metal` ctest. No Metal code is built/tested anywhere. Everything below is unverified until this lands. | missing | **BLOCKER** | `CMakeLists.txt:169-181` | L (HW-gated) |
| **M1** | STRUCTURAL ROOT: `create_buffer`/`create_texture` return STUB handles; `lookup_buffer`/`lookup_texture` **hard-code `return nil`**. Every copy / vertex-index bind / draw_indexed / upload / download / texture-view / descriptor-write is therefore a DEAD PATH (graceful no-op). | stub | **BLOCKER** | `MetalDevice.mm:1498,1505` | L |
| **M2** | `create_graphics_pipeline` always calls `build_sprint1_triangle_pipeline` — hardcoded inline-MSL triangle; ignores vertex layout/blend/depth-stencil/topology/shader handles. | stub | **HIGH** | `MetalDevice.mm:512-549`, `MetalPipeline.mm:90-140` | XL |
| **M3** | MSL shader path: only inline raw-MSL `newLibraryWithSource:`. No SPIRV-Cross MSL wiring; engine ships SPIR-V → Metal can't consume it. Needs `MetalShaderToolchain` (analog of `D3D12ShaderToolchain`) + `cd::spirv_cross_glue` link in CMake + `add_msl_resource_binding()` index fixup. | missing | **HIGH** | `MetalDevice.mm:388-421`; `MetalInternal.hpp:51`; CMake deps `:185-194` | XL |
| **M4** | Descriptor set update is validate-only (writes nothing to the argument encoder); `bind_descriptor_set` is a no-op; `allocate_descriptor_set` is a 1-slot dummy. | partial | HIGH | `MetalDevice.mm:752-777,634-707`; `MetalInternal.hpp:639` | L |
| **M5** | `barrier()` empty no-op. Metal auto-manages most hazards but heap/untracked + fence-based barriers still needed. | missing | MED | `MetalInternal.hpp:701-702` | M |
| **M6** | Compute dispatch hardcodes `threadsPerThreadgroup = 1×1×1`. | partial | MED | `MetalCommandBuffer.mm:652` | M (folds with M3) |
| **M7** | Swapchain: single color attachment, no resize/out-of-date recovery, no HDR/PQ. | partial | MED | `MetalSwapchain.mm:19-26,60-102` | M |
| **M8** | Render pass: color[0]-only resolve, no depth attachment, topology hardcoded triangle. | partial | HIGH | `MetalCommandBuffer.mm:98-103` | M |
| **M9** | RT (accel-struct / RT-pipeline / `dispatch_rays`) TOTALLY ABSENT → falls to `IDevice` default `kNotImplemented`. Metal RT model (`id<MTLAccelerationStructure>` + intersector / visible-function-table) is structurally different from the Vulkan SBT — ground-up design. | missing | HIGH (RT) | `MetalDevice.mm` (no override); `update_descriptor_set` rejects `kAccelerationStructure` `:758-767` | XL |
| **M10** | Mesh-shader pipeline + `draw_mesh_tasks` absent (Metal object/mesh stages exist, unwired). | missing | MED | no override | L |
| **M11** | Bindless texture array absent (Metal argument-buffer tier2 supports it natively, unwired). | missing | MED | no override | L |
| **M12** | `hello_metal` sample is boot-smoke only (device create + stub buffer/texture + wait_idle); no window/swapchain/draw/present. Needs NSWindow/NSView + CAMetalLayer host. | partial | MED | `samples/rhi/hello_metal/main.cpp:15,27-114` | M |

**Already real (likely parity once compiled + tested):** sampler
(`MetalPipeline.mm:222-267`), fence (`dispatch_semaphore`), binary +
timeline semaphore (`MTLSharedEvent`), queue submit ordering, `wait_idle`,
push constants. These need verification, not reimplementation.

**Ordered Metal work items** (foundation → resources → shaders → pipeline →
render → RT, each gated on M0 building):

1. **M0 — macOS build + test infra** (L, HW-gated). Enable
   `CD_RHI_METAL_ENABLED` on Apple, compile all `.mm`, get `hello_metal`
   ctest green on a real Mac. **Nothing below is verifiable without this.**
2. **M1 — real `MTLBuffer`/`MTLTexture` registry** (L). `newBufferWithLength`/
   `newTextureWithDescriptor` + a real handle→object map; make
   `lookup_buffer`/`lookup_texture` return live objects. This single fix
   resurrects the already-written copy / bind / upload / download / view
   bodies that are currently dead paths.
3. **M3 + M6 — `MetalShaderToolchain` (SPIR-V→MSL)** (XL). Add `cd::shader`
   + `cd::spirv_cross_glue` to `rhi_metal` CMake deps; compose
   GLSL→SPIR-V→MSL; feed MSL into `create_shader_module`; configure
   `add_msl_resource_binding()` for `[[buffer(n)]]/[[texture(n)]]/[[sampler(n)]]`
   index fixup; read real `threadsPerThreadgroup` from reflection.
4. **M4 — argument-buffer descriptor writes + `bind_descriptor_set`** (L).
   Real per-binding layout emission + encoder writes.
5. **M2 + M8 — real `create_graphics_pipeline` + depth render pass** (XL).
   Compose `MTLRenderPipelineDescriptor` from the desc (vertex layout,
   blend, depth-stencil, topology, shader handles); wire depth attachment.
6. **M5 — fence-based barriers** (M) for heap/untracked resources.
7. **M7 + M12 — swapchain resize/HDR + windowed `hello_metal`** (M+M).
8. **M9 — Metal RT** (XL). `id<MTLAccelerationStructure>` BLAS/TLAS +
   intersection-function-table; map the `dispatch_rays`/SBT contract onto
   Metal's intersector model. Requires its own ADR (geri-dönülemez RT
   abstraction decision — Metal RT ≠ SBT model).
9. **M10 — mesh shaders** (L) + **M11 — bindless argument-buffer tier2** (L).

---

## §4. Recommended FIRST workflow

**V1 — depth-aware barrier aspect mask (Vulkan).**

- **Why first**: Vulkan-first per directive; smallest blast radius (one
  function + one `ResourceTables` field); highest immediate value (it is a
  *live correctness bug* — every depth-attachment barrier emits the wrong
  subresource and trips validation / no-ops silently). It is fully
  autonomous (no hardware/signoff gate) and the asymmetry with the
  already-correct device-side readback (`aspect_for_format`) makes the fix
  unambiguous.
- **Scope**: add texture-handle→`Format` map to `ResourceTables`; replace
  hardcoded `VK_IMAGE_ASPECT_COLOR_BIT` at `VulkanCommandBuffer.cpp:671`
  with `aspect_for_format(format)`; add a depth-texture barrier regression
  gtest.
- **Owner chain**: architect (interface: where the format map lives on
  `ResourceTables`) → developer → tester → build-devops (verify lavapipe +
  ctest green).
- **Exit gate**: 254/254+ ctest PASS, no new Vulkan validation errors on a
  `D32_SFLOAT` transition, lavapipe sample-smoke clean.

After V1, proceed to **V2 (multi-queue)** to finish Phase A, then freeze
the bar and open Phase B with **D12 (wire DXIL toolchain)**.

---

## §5. Blocking prerequisites (autonomous vs user-gated)

| Item | Type | Gates | Autonomous? |
| ---- | ---- | ----- | ----------- |
| V1, V2, V3 (Vulkan) | none | — | **YES** — lavapipe SW Vulkan in CI is sufficient |
| D1–D14 (D3D12 functional + DXR code) | none | windows-2025 GHA runner builds + runs ctest; DXC/DXIL via Windows SDK | **YES** — code lands + compile-verifies; runtime DXR `create_*` returns `kNotImplemented` on GHA (no DXR GPU) but that is the documented capability-gate, not a failure |
| D7/D9 runtime DXR *verification* | hardware | DXR-capable GPU (NVIDIA RTX / AMD RDNA2+ / Intel Arc, RaytracingTier 1.0+) + runtime `dxcompiler.dll` | **NO** — needs NVIDIA self-hosted CI lane (currently **PENDING registration**, `ci-nvidia-windows.yml:28`) |
| D16 (D3D12 GPU golden parity) | hardware + signoff | NVIDIA self-hosted runner online + `hello_d3d12_pbr` on cross-compiled corpus | **NO** — operator must register runner; user signoff on golden tolerance |
| Lavapipe golden activation | operator action | trigger `capture_lavapipe_goldens=true` dispatch once, commit PNGs | **NO** — 30-min operator action; cheapest pixel-regression gate to activate |
| M0 (Metal build/test) | hardware + signoff | macOS + Apple Silicon (GHA `macos-14` runners CANNOT access Metal GPU — QEMU virtualized); needs **self-hosted Apple Silicon runner** OR local dev Mac | **NO** — user must provide Mac hardware / runner; **Fork-A sign-off** referenced in Metal ADR §8.5 still pending |
| M9 (Metal RT) | signoff (ADR) | geri-dönülemez RT abstraction decision (Metal intersector model ≠ Vulkan SBT) | **NO** — architect ADR + user approval before implementation |

**Summary of gates**: All of **Phase A (Vulkan V1+V2)** and the **entire
D3D12 code-landing set (D1–D14)** are fully autonomous and verifiable in
existing CI. The first user/hardware gate is D3D12 *runtime RT
verification* + golden parity (NVIDIA self-hosted runner). **All of Metal
(M0+) is hardware/signoff-gated** on Mac availability and the pending
Fork-A sign-off.

---

## §6. Out-of-scope (tracked, not part of this parity bar)

These are deliberately excluded because Vulkan (the bar) lacks them; they
are future SOTA milestones, not parity work:

- **Indirect draw / dispatch** — needs a new `ICommandBuffer` surface
  (`draw_indirect`/`draw_indexed_indirect`/`dispatch_indirect`) + all three
  backends. Engine-wide GPU-driven-rendering RFC.
- **GPU AS compaction** + **in-place AS refit** — RT performance milestone;
  requires `AccelStructureDesc` flag plumbing across all backends.
- **OpenGL backend** — explicitly boot-only (59 `kNotImplemented`),
  deferred indefinitely per `D3D12_PARITY_AUDIT.md` Phase 120+.
- **Slang single-source compiler** — `SlangCompilerStub` returns nullptr;
  would eliminate SPIRV-Cross but needs the full Slang build tree (XL).

---

## §7. Execution ordering (one line)

```
PHASE A (Vulkan, autonomous):  V1 → V2  [→ V3 deferred/ADR-note]
PHASE B (D3D12, autonomous code; HW-gated verify):
    D12 → D1+D15 → D3 → D5 → D4 → D6 → D2 → (D13 ∥ D14)
        → D8 → D7 → D9 → D10 → D11 → [D16 HW-gated]
PHASE C (Metal, HW+signoff-gated):
    M0 → M1 → M3+M6 → M4 → M2+M8 → M5 → M7+M12 → [M9 ADR] → M10 → M11
```

Bar is **frozen** at the end of Phase A. D3D12 and Metal each target the
exact §0 capability table — no more, no less.
