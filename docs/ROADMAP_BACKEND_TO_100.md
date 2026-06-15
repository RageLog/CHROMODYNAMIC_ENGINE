# ROADMAP — Backend to 100% (close everything, then maintenance-only)

> Source: road-to-100 audit (Workflow, 6 agents, 2026-06-16).

## Definition of Done

## Definition of Done — "Backend chapter = 100%"

The Backend chapter is DONE (only bug-fix + maintenance after) when BOTH tiers below are satisfied. Honesty requires splitting into two tiers because one is fully reachable on this Windows host NOW and the other needs hardware the user must provide.

### TIER 1 — IMPL-100% (reachable NOW on this Windows host; this is the deliverable the marathon can fully close)
Every RHI capability is in exactly one of three TERMINAL states — no fourth "open/TODO" state may remain:
1. **IMPLEMENTED on all 3 backends** (Vulkan + D3D12 + Metal), with a real override (not a kNotImplemented hole), OR
2. **Formally CLOSED-AS-WONTFIX** via an ADR decision record (not just roadmap prose) that states the rationale, the parity-safety argument, and the promote-on-request trigger, OR
3. **Capability-GATED kNotImplemented** that is documented in the single-source-of-truth `kNotImplemented inventory` as a runtime feature-gate (adapter lacks DXR/mesh/VRS/etc.), NOT a code hole.

Plus the meta-conditions:
- **Zero TODO-holes**: a kNotImplemented inventory table exists listing EVERY kNotImplemented/silent-no-op site on all 3 backends, each labelled gate | intentional-defer(ADR) | implemented. Count of "TODO-hole" = 0.
- **Docs are truthful**: every doc statement matches the shipped code. Specifically the false "SBT kNotImplemented on all three backends" claim is corrected (it is real on Vulkan+D3D12, absent on Metal); the stale `D3D12_PARITY_AUDIT.md` and `RHI_PARITY_STATUS.md` are refreshed or stamped historical; the ADR-X6 stale `hello_rt` reference is fixed.
- **Every implemented/fixed feature has a test** that fails on temp-revert (project evidence rule), runnable on this host (lavapipe/WARP/CPU) — Metal GPU tests authored behind `#if __APPLE__` count as authored-but-mac-run.
- **Build clean** `cmake --build --preset ninja-debug` + **ctest green** on this host at every checkpoint.

### TIER 2 — VERIFY-100% (GPU pixel/runtime verification; partly runnable here, partly HARD-blocked on hardware)
- **Vulkan**: golden-image regression captured + committed + gating in CI (lavapipe software goldens NOW on this host; RTX-3080 hardware goldens NOW since the host has an RTX 3080).
- **D3D12**: `test_backend_pixel_parity` (D3D12-vs-Vulkan readback diff) + DXR command/dispatch_rays run GREEN on this host's **RTX 3080** (this is NOW-runnable — the older "nvidia-gated, cannot close here" label is obsolete; the host HAS the GPU).
- **Metal**: `CD_RHI_METAL_ENABLED=ON` compiles -Werror-clean on Apple Clang, the 9 Tier-2 GPU tests + cull-parity test run green, windowed hello_metal runs, and a Metal golden enters the cross-backend matrix. **This is HARD-BLOCKED — needs Apple-Silicon hardware/runner the user must provide + Fork-A sign-off.**

### Plain-language bottom line
- **IMPL-100% is achievable autonomously NOW** (Waves 1–5 below). After it merges, the Vulkan + D3D12 backends are 100%-implemented AND 100%-GPU-verified on this host (RTX 3080), and Metal is 100%-implemented + structurally-reviewed + host-toolchain-verified, with its GPU verification as the single named residual.
- **"Only bug-fix + maintenance after" becomes literally true once IMPL-100% merges AND the host-runnable GPU verification (Vulkan + D3D12 on the RTX 3080) is green.** The ONLY thing left after that is the Metal-on-Mac GPU verification, which is not code work — it is a hardware-gated sign-off that no amount of coding on this host can retire. The user supplies a Mac (or self-hosted Apple-Silicon runner) → run the already-authored Metal GPU tests → Tier 2 closes. Nothing stays "open" in the design/implementation sense.

## Hard blockers (HW-gated)

## Hard hardware blockers (cannot be 100%-VERIFIED on this Windows host) vs what reaches 100%-IMPLEMENTED now

### CORRECTION to the older audits (load-bearing)
This host has an **NVIDIA RTX 3080 Laptop GPU** (confirmed via Win32_VideoController) + Vulkan ICD (`vulkan-1.dll`) + D3D12 (`d3d12.dll`) + dxcompiler. The earlier audits labeled D3D12 pixel-parity and DXR-GPU verification "nvidia-gated / cannot close on this host" — that is **OBSOLETE**. The host HAS the GPU. So:
- D3D12-vs-Vulkan pixel parity (`test_backend_pixel_parity`) → **RUNNABLE NOW**.
- DXR dispatch_rays / CreateStateObject / Vulkan VK_KHR_ray_tracing_pipeline → **RUNNABLE NOW**.
- AS compaction/refit ray-equivalence → **VERIFIABLE NOW**.
- Vulkan hardware goldens → **CAPTURABLE NOW**.
None of these is a hard blocker. They are Wave 2, the highest-value autonomous action.

### THE ONE TRUE HARD BLOCKER: Apple hardware (Metal)
Cannot be retired by ANY amount of code or docs on this Windows host (CD_RHI_METAL_ENABLED auto-forces OFF on non-Apple; the .mm files have never seen Apple Clang):
1. **D-METAL-COMPILE** — first -Werror-clean Obj-C++ compile on Apple Clang. Until this passes there is literally zero evidence the backend compiles.
2. **D-METAL-GPU** — the 9 Tier-2 GPU tests + cull-parity test + windowed hello_metal + Metal-validation-clean + real-device caps confirmation + verifying the M-MSAA/M-STENCIL/M-DEPTHBIAS/M-HDR-EDR/M-BCN drafts actually draw correct pixels.
3. **D-METAL-PARITY** — the Vulkan/D3D12-vs-Metal cross-backend golden (the matrix has NO Metal arm at all today).
4. **D-ARGBUF-TIER** — argument-buffer Tier-1 fork decision needs a real device to confirm.

Requires: an Apple-Silicon Mac (or self-hosted Apple-Silicon CI runner — note GitHub's hosted macos-14 is virtualized and CANNOT access a Metal GPU) + the pending **Fork-A sign-off**. This is hardware + a human decision, not engineering on this host.

### SOFT/operator blockers (not hardware-impossible, just need a one-time action)
- **VK validation layer on the CI lane** (D-VK-VALIDATION-LANE): install VK_LAYER_KHRONOS_validation on the lavapipe lane → the validation-clean gate goes live. Operator action, not hardware.
- **NVIDIA self-hosted runner registration** (D-NVIDIA-RUNNER): the host can run RT now, but a *standing* CI gate needs the PENDING self-hosted runner registered. Operator action.
- **HDR display** for true PQ/EDR pixel eyeballing (D-HDR-SWAPCHAIN, M-HDR-EDR): the API wiring + CheckColorSpaceSupport are verifiable on any monitor; only the final HDR pixel sign-off wants an HDR panel. Low priority.

### What reaches 100%-IMPLEMENTED now (no hardware needed)
ALL of GROUP A (implement), GROUP B (close-as-wontfix ADRs), and GROUP C (tests, incl. Metal GPU tests AUTHORED behind `#if __APPLE__`) are windows-now. After Waves 0-4:
- Interface, Vulkan, D3D12 = **100% implemented + 100% GPU-verified on the RTX 3080**.
- Metal = **100% implemented + structurally reviewed + host-MSL-toolchain-verified + all GPU tests authored**, awaiting only execution on Apple hardware.
- Zero kNotImplemented TODO-holes; every kNotImplemented is either a documented feature-gate or an ADR-backed intentional-defer.

### Plain bottom line for "only bug-fix + maintenance after"
That statement becomes literally true the moment IMPL-100% (Waves 0-4) merges AND the host-runnable GPU verification (Wave 2: Vulkan + D3D12 on the RTX 3080) is green. The SOLE remaining residual is **Metal-on-Mac GPU verification** — which is not open design/implementation work, it is a hardware-gated execution of already-authored tests. Supply a Mac → run → done. Nothing in the engine's RHI design or code stays "open."

## Completion percentages (baseline)

## Completion-% — per-backend, RHI sub-components, engine module groups

> Two-axis honesty: **IMPL%** = code written + on the parity bar; **VERIFY%** = GPU/pixel-proven. Host has RTX 3080 → Vulkan+D3D12 VERIFY is runnable NOW; Metal VERIFY is mac-gated. Reconciled across all 5 audits; supersedes the stale doc tables.

### A. Per-backend (the headline)

| Backend | IMPL% | VERIFY% | Rationale (one line) |
|---|---|---|---|
| **Interface (IDevice/ICommandBuffer)** | 98% | n/a | 66 virtuals, Result<>/[[nodiscard]] clean; only missing surfaces are indirect-draw + query-pool + AS-flags (this plan adds them). |
| **Vulkan (reference)** | 95% | 80% | Zero code-path stubs; all 7 kNotImpl are feature-gates. Gaps: debug_group_depth + 3 feature flags + query subsystem + indirect + AS compaction/refit + MSAA-resolve. Golden not yet captured (RTX 3080 here can). |
| **D3D12** | 90% | 35% → (NOW-runnable to ~90% here) | No TODO stubs; 14 kNotImpl are feature-gates. Real gaps: SRV-MS, indexed-BLAS, HDR swapchain, resize, format table 13/43. Pixel-parity test exists but never run green here — **RTX 3080 can run it now.** |
| **Metal** | 60% | **0% (mac-gated)** | Large reviewed .mm body, NEVER compiled on Apple Clang / run on GPU. Genuinely missing even in source: MSAA-resolve, stencil, depth-bias, HDR-EDR, timestamps, RT-pipeline. Only host MSL toolchain is verified. |
| **OpenGL** | 25% | 0% | Out-of-charter — CLOSE-AS-WONTFIX (B-OPENGL); stop counting as open. |

### B. RHI sub-components (cross-backend)

| Sub-component | IMPL% | Notes |
|---|---|---|
| Resources (buffer/texture/views) | 95% | D3D12 format table 13/43 + per-subresource state are the gaps. |
| Pipelines (gfx/compute/mesh) | 95% | Mesh-shader feature-gated; pipeline cache unwired (A-PIPECACHE). |
| Descriptors + bindless | 92% | Vulkan/D3D12 solid; Metal argument-buffer Tier-1 edge unhandled. |
| Sync (binary + timeline) | 90% | Timeline complete; D3D12 binary-sema inert (B-D3D12-SYNC). |
| Swapchain + HDR | 80% | Vulkan full; D3D12 + Metal ignore colour_space + no resize (D3D12). |
| Render pass / MSAA resolve | 70% | Resolve attachment unset on Vulkan + Metal; D3D12 StoreOp dropped (benign). |
| RT — ray-query (production) | 90% | All 3 backends; nvidia-here-verifiable. |
| RT — SBT pipeline | 67% (2 of 3) | Vulkan+D3D12 real, Metal absent — the mislabeled asymmetry. |
| RT — AS compaction/refit | 0% | Not expressible (no build_flags field); A-AS-FLAGS+A-COMPACTION+A-REFIT. |
| GPU query subsystem | 0% | Feature flags exist, no API — A-QUERY. |
| Indirect draw/dispatch | 0% | Enums round-trip, no method — A-INDIRECT. |
| Parallel render pass | 90% | Vulkan true-secondary + D3D12 replay (det. 100%); Metal mac-gated. |
| Readback (image→buffer) | 90% | Blocking done+tested; async deferred (B-NULL-NONBLOCK-READBACK). |
| Debug/observability | 85% | D3D12 tracks debug_group_depth+test; Vulkan missing (V-DBGDEPTH). |
| Cross-backend pixel parity | 30% | Vk==D3D12 single scene; no Metal arm; goldens dormant. |

### C. Engine module groups (broader context)

| Group | IMPL% | Autonomy of remaining work |
|---|---|---|
| foundation (22 libs) | 92% | windows-now (polish) |
| math | 90% | windows-now |
| memory | 85% | windows-now (TSan unavailable = platform gap) |
| concurrency | 88% | windows-now |
| io + vfs | 85% | windows-now |
| asset (10 libs) | 80% | windows-now (corrupt-asset fuzz) |
| ecs | 85% | windows-now (scheduler/change-detection depth) |
| scene | 80% | windows-now |
| shader + gluon | 85% | windows-now (Slang stub = future XL) |
| material | 85% | nvidia-here-verified (Vulkan) |
| framegraph | 70% | windows-now — biggest architectural lever (auto-barrier/aliasing graph) |
| composite/bloom/TAA/velocity | 88% | nvidia-here (Vulkan-verified) |
| SSR/GTAO/IBL | 85% | nvidia-here |
| light/atmosphere/clouds/shafts | 85% | nvidia-here (atmosphere lib thin, 273 LOC) |
| DDGI | 70% | nvidia-here (real GPU compute, wired phase1213) |
| ReSTIR-DI | 70% | nvidia-here (real dispatch + SVGF) |
| ReSTIR-GI | 25% | header-only, no dispatch — research-skeleton |
| NRC | 15% | header-only skeleton — research, not a feature |
| postfx-extras (decal/particles/mesh/VG/VT/synth) | 80% | nvidia-here (VG/VT are demo-tier vs Nanite) |
| camera | 90% | windows-now |
| anim (+IK +graph) | 75% | windows-now (CPU-LBS; GPU skinning unconfirmed) |
| audio (+DSP +spatial) | 80% | windows-now |
| physics (Jolt + vehicle + soft-body) | 70% | windows-now (sync-loop maturity) |
| debug_line / debug_draw | 90% | nvidia-here (Vulkan; needs D3D12/Metal port) |
| editor (40 ui libs, ~35k LOC) | 70% | windows-now (no single editor.exe entry point) |
| sample_framework | 85% | windows-now |
| hello_engine (showcase) | 90% as demo | windows-now (main.cpp 9658 LOC, consolidation unmet) |

> **Backend-chapter rollup:** Interface 98% + Vulkan 95% + D3D12 90% + Metal 60% IMPL. Executing GROUP A/B/C → **IMPL-100% on all 3 backends**. VERIFY: Vulkan+D3D12 reach ~95%+ on the RTX 3080 here; Metal VERIFY stays 0% until a Mac exists.

## Plan

# docs/ROADMAP_BACKEND_TO_100.md — Road to a Defensible 100% (3-Backend RHI)

> Supersedes the gap TABLES in ROADMAP_BACKEND_PARITY / ROADMAP_BACKEND_COMPLETION / D3D12_PARITY_AUDIT / RHI_PARITY_STATUS (all stale in opposite directions). Trust the source; this doc was written against verified source at commit ~50934d6.
>
> **Host fact that re-scopes everything:** this Windows host has an **NVIDIA RTX 3080 Laptop GPU** + Vulkan ICD + D3D12 + dxcompiler. Therefore Vulkan AND D3D12 GPU verification (golden capture, DXR dispatch, cross-backend pixel parity) are **runnable here NOW** — they are NOT hard blockers. The ONLY hard hardware blocker is **Metal (needs a Mac)**.
>
> Decisiveness rule: every previously-"deferred" item below is marked **IMPLEMENT** or **CLOSE-AS-WONTFIX** — never "maybe".

Each item: `id | action | files | effort (S/M/L/XL) | autonomy`. Autonomy = `windows-now` (do it here), `nvidia-here` (needs a GPU, the host HAS one → runnable now), `mac-gated` (true hard blocker).

---

## GROUP A — IMPLEMENT-NOW (windows-now code; closes real capability gaps)

### A.1 Cross-cutting capability subsystems (all 3 backends)

- **A-QUERY** | IMPLEMENT GPU query subsystem (timestamp / pipeline-statistics / occlusion). Add `QueryPoolHandle` + `create_query_pool` + `get_query_results` to IDevice; `write_timestamp`/`begin_query`/`end_query` to ICommandBuffer. Vulkan `VkQueryPool`+`vkCmdWriteTimestamp2`+`vkGetQueryPoolResults`; D3D12 `CreateQueryHeap`+`ResolveQueryData`; Metal `MTLCounterSampleBuffer`. Without this the timestamp/pipeline-statistics feature flags are meaningless. | `include/cd/rhi/{IDevice,ICommandBuffer}.hpp`, `src/{vulkan,d3d12,metal}/*` | **L** | windows-now (lavapipe+WARP run timestamps in SW; Metal arm authored)
- **A-INDIRECT** | IMPLEMENT indirect draw/dispatch. The scaffolding enums already round-trip on all 3 backends (`Enums.hpp:217 kIndirect`, `:264 kIndirectArgument`) with NO consuming method — a reviewer WILL ask why. Add `draw_indirect`/`draw_indexed_indirect`(+count-buffer) to IDrawRecorder and `dispatch_indirect` to ICommandBuffer, default no-op base, real overrides: Vulkan `vkCmdDrawIndirect`/`vkCmdDrawIndexedIndirectCount`/`vkCmdDispatchIndirect`; D3D12 `ExecuteIndirect`+`CommandSignature`; Metal `drawPrimitives:indirectBuffer:`/`dispatchThreadgroupsWithIndirectBuffer:`. | interface + 3 backends + Null | **L** | windows-now (record+submit, no-validation-error)
- **A-AS-FLAGS** | IMPLEMENT AS build-flag plumbing (prerequisite for A-COMPACTION + A-REFIT). Add `AccelBuildFlags{kAllowUpdate,kAllowCompaction,kPreferFastTrace,kPreferFastBuild}` to `AccelStructureDesc` (currently has NO build_flags field — grep clean). Thread to all 3 backends' build calls. | `include/cd/rhi/Descriptors.hpp` + 3 backends | **M** | windows-now
- **A-COMPACTION** | IMPLEMENT AS memory compaction. On `kAllowCompaction`: query COMPACTED_SIZE → allocate smaller AS → copy-compact. Vulkan `vkCmdWriteAccelerationStructuresPropertiesKHR`+`vkCmdCopyAccelerationStructureKHR(MODE_COMPACT)`; D3D12 `EmitRaytracingAccelerationStructurePostbuildInfo`+`CopyRaytracingAccelerationStructure(COMPACT)`; Metal `copyAndCompactAccelerationStructure:`. | 3 backends | **L** | windows-now to BUILD/record; correctness (sizes shrink, rays still hit) verified on the RTX 3080 here (nvidia-here)
- **A-REFIT** | IMPLEMENT in-place AS refit. On `kAllowUpdate`: `MODE_UPDATE` with src=dst using updateScratchSize. The skinned-mesh BLAS rebuilds every frame (CesiumMan) → real engine win. Vulkan `MODE_UPDATE`; D3D12 `PERFORM_UPDATE`; Metal `refitAccelerationStructure:`. | 3 backends | **M-L** | windows-now to record; ray-equivalence verified on RTX 3080 here (nvidia-here)
- **A-BINDPOINT** | HARDEN bind-point routing: replace the implicit "which-layout-handle-is-nonnull" heuristic with an explicit `BindPoint{kGraphics,kCompute,kRayTracing}` tracked per command buffer on all 3 backends; document the RT-aliases-compute convention at the interface. | `ICommandBuffer.hpp` + 3 backends | **M** | windows-now
- **A-RESULT-DIAG** | HARDEN the silent-no-op-on-bad-handle recording contract: keep void API but add a uniform `CD_RHI_DEBUG` assert + a queryable per-CB `recording_error()` flag (mirrors debug_group_depth pattern) on all 3 backends so a stale handle is LOUD in debug. Document the contract on ICommandBuffer. | `ICommandBuffer.hpp` + 3 backends | **M** | windows-now

### A.2 Vulkan (reference backend) parity + missing surfaces

- **V-DBGDEPTH** | IMPLEMENT `debug_group_depth()` override (currently inherits base `return 0`; D3D12 tracks it + has a test). Add `debug_group_depth_` member, ++ in push, guarded -- in pop, reset in begin(), override getter. | `src/vulkan/VulkanCommandBuffer.{hpp,cpp}` | **S** | windows-now
- **V-FEAT-TS** | IMPLEMENT `features_.timestamp_queries = p.limits.timestampComputeAndGraphics != 0` (D3D12 sets it; Vulkan leaves it false). | `VulkanDevice.cpp:~4571` | **S** | windows-now
- **V-FEAT-PS** | IMPLEMENT `features_.pipeline_statistics_queries = f.pipelineStatisticsQuery != 0` (read but ignored). | `VulkanDevice.cpp:~4591` | **S** | windows-now
- **V-FEAT-VRS** | IMPLEMENT `features_.variable_rate_shading = has_ext("VK_KHR_fragment_shading_rate")` AND add the command surface (see A.4 VRS decision). | `VulkanDevice.cpp:~4599` | **S** | windows-now
- **V-MSAA-RESOLVE** | IMPLEMENT render-pass MSAA resolve: `begin_rendering_` hardcodes `resolveMode=NONE` + null resolve attachment + null pStencilAttachment. Wire resolve attachment from RenderPassDesc + stencil attachment. | `src/vulkan/VulkanCommandBuffer.cpp:305,344` | **M** | windows-now
- **V-COPY-IMG** | IMPLEMENT `copy_texture_to_texture` (image→image `vkCmdCopyImage`/`vkCmdBlitImage`) — only buffer↔buffer / buffer↔image exist today; blocks mip-gen / image blit. | `ICommandBuffer.hpp` + 3 backends | **M** | windows-now
- **V-MULTIVIEW** | IMPLEMENT multiview/viewMask plumbing (currently hardcoded viewMask=0, layerCount=1) for stereo/cubemap-in-one-pass. | `VulkanCommandBuffer.cpp:344-345` | **M** | windows-now (CLOSE-AS-WONTFIX instead if VR is out of charter — see B)
- **V-PIPECACHE** | IMPLEMENT `VkPipelineCache` create/serialize/deserialize-to-disk (PipelineCacheKey.hpp exists at interface, not wired) — kills cold-start hitching. | `src/vulkan/*` + D3D12 `ID3D12PipelineLibrary` + Metal `MTLBinaryArchive` | **M** | windows-now

### A.3 D3D12 parity + coverage

- **D-SRV-MS** | IMPLEMENT Texture2DMS SRV (the named A1 follow-up): SRV path at `D3D12Device.cpp:1015` hardcodes `DIMENSION_TEXTURE2D` while RTV/DSV (912/957) correctly branch to MS. Add `is_ms` branch to the 5 SRV sites (1015/2974/3109/3174/3274). | `src/d3d12/D3D12Device.cpp` | **S** | windows-now (WARP)
- **D-BLAS-INDEXED** | IMPLEMENT indexed BLAS: `:4231` hardcodes `IndexFormat=UNKNOWN`, `:4235 IndexBuffer=0` — the interface's index_buffer/offset/count are dropped (Vulkan honors them). Resolve index GVA + map IndexType→DXGI R16/R32. | `D3D12Device.cpp:4231-4235` | **S** | windows-now build / nvidia-here trace
- **D-HDR-SWAPCHAIN** | IMPLEMENT HDR colour-space: `create_swapchain` never reads `desc.colour_space` nor calls `SetColorSpace1` (chain3 already in hand at :3779). Map ColorSpace→DXGI_COLOR_SPACE + `CheckColorSpaceSupport`. | `D3D12Device.cpp:3722-3793` | **M** | windows-now (API+CheckColorSpaceSupport verifiable; PQ pixels display-gated)
- **D-SWAPCHAIN-RESIZE** | IMPLEMENT `kSwapchainOutOfDate` + `ResizeBuffers`: present maps ALL failures to kDeviceLost, no resize path — window resize on D3D12 won't trigger engine recreate. | `D3D12Device.cpp:3679-3718` | **M** | windows-now
- **D-FORMAT-MAP** | IMPLEMENT full format table: `to_dxgi_format` (86-107) maps ~13 of 43 interface Formats; the rest fall to UNKNOWN→reject. Add remaining (R8Unorm, R16Float, RG16Float, RGBA16*, all *Uint/*Sint, RGBA8Snorm, D16; depth needs TYPELESS + typed views) + BCn block-compressed. | `D3D12Device.cpp:86-107` | **M** | windows-now
- **D-ROOTCOST** | HARDEN root-signature push-constant cost: `create_pipeline_layout` SILENTLY clamps pc_dwords when union root cost >64 DWORDs (same silent-truncation class as the BLAS-cap lesson). Return `kInvalidArgument` instead of clamping. | `D3D12Device.cpp:1666-1671,5563` | **S** | windows-now
- **D-MIPSTATE** | IMPLEMENT per-subresource state tracking (currently single whole-resource `TextureRecord.state`, ALL_SUBRESOURCES only) so mip-chain gen (read mip N / write N+1) works — Vulkan tracks per-subresource. | `D3D12Device.cpp` | **M** | windows-now

### A.4 Metal — windows-now slice (host-verifiable C++/logic + drafts)

- **M-EVENT-ATOMIC** | HARDEN `MetalEventObj::signal_counter_` (plain uint64 mutated by `++` across threads, no sync) → `std::atomic<uint64_t>` fetch_add. | `src/metal/MetalInternal.hpp:560-575` | **S** | windows-now (header-only C++)
- **M-BCN-PITCH** | HARDEN BCn bytesPerRow: copy passes `bytesPerRow:0` (valid only uncompressed); device readback computes `bytes_per_block*width` WITHOUT /4 block → 4× too large. Use `max(1,(w+3)/4)*block_bytes`. Plain C++ over `info_of(Format)` (compiles on Windows). | `MetalCommandBuffer.mm:742,797`, `MetalDevice.mm:2215-2225` | **M** | windows-now (logic + unit test; GPU upload mac-gated)
- **M-CPP-STUB** | HARDEN dead `MetalDevice.cpp` `__APPLE__` branch: returns `kNotImplemented` "skeleton" — change to `kBackendInitFailed`, drop skeleton header comment. | `src/metal/MetalDevice.cpp:19-23` | **S** | windows-now
- **M-DOC-PUSHIDX** | HARDEN stale comment "push index 16" — real default is 8. | `MetalDevice.mm:2813` | **S** | windows-now
- **M-BANNER** | HARDEN overstated "27/27 implemented, 0 kNotImpl" comment/doc — there is no banner code and mesh/RT/bindless legitimately return kNotImplemented (feature-gated). Correct to "feature-complete with HW-gated kNotImplemented branches". | `MetalDevice.mm:156-163,3414`, `docs/METAL_MAC_TESTING.md:3` | **S** | windows-now
- **M-MSAA-RESOLVE** *(draft now, verify on Mac)* | IMPLEMENT MSAA resolve: `begin_render_pass` never sets resolveTexture/MultisampleResolve though pipeline honors samples. | `MetalCommandBuffer.mm:106-217` | **M** | windows-now draft / mac-gated verify
- **M-STENCIL** *(draft now)* | IMPLEMENT stencil ref + front/back stencil ops (only depthCompare/depthWrite set today). | `MetalPipeline.mm:736-746` | **M** | windows-now draft / mac-gated verify
- **M-DEPTHBIAS** *(draft now)* | IMPLEMENT depth bias / slope-scale / clamp + MTLDepthClipMode (ignored → shadow acne). | `MetalPipeline.mm` | **M** | windows-now draft / mac-gated verify
- **M-HDR-EDR** *(draft now)* | IMPLEMENT HDR/EDR CAMetalLayer (wantsExtendedDynamicRange/colorspace/EDRMetadata) — colour_space ignored → silent SDR. | `MetalSwapchain.mm:60-101` | **M** | windows-now draft / mac+HDR-display verify
- **M-RT-PIPELINE** | IMPLEMENT Metal RT-pipeline/SBT to reach true 3-way parity (Vulkan+D3D12 have it; Metal `:1198` is the ONLY backend missing it). `MTLRenderPipelineState`-equivalent visible-function-table + intersection-function-table + SBT. | `MetalDevice.mm`, `MetalCommandBuffer.mm` | **XL** | windows-now draft / mac-gated verify. *(Alternative: CLOSE-AS-WONTFIX per B-RT-SCOPE — pick one, do not leave ambiguous.)*

---

## GROUP B — CLOSE-AS-WONTFIX (formal ADR decisions so they stop reading as "open")

Each gets a one-paragraph ADR (`docs/ADR/ADR-20260616-*.md`, Iglberger format) with rationale + promote-on-request trigger. After the ADR, the item is CLOSED, not open.

- **B-RT-SCOPE** | DECISION: ray-query (inline) is the PRODUCTION RT path and is on all 3 backends. SBT RT-pipeline is an EXTRA capability present on Vulkan+D3D12. **Pick ONE and write it down:** either (a) M-RT-PIPELINE implements Metal SBT for true parity, OR (b) declare SBT explicitly off the Metal parity bar. **Recommendation: (b) CLOSE** Metal SBT as out-of-charter, BUT correct the false premise — the ADR must NOT claim "Vulkan's create_rt_pipeline is a stub" (it is a full body at `VulkanDevice.cpp:4351`). | ADR + `ROADMAP_BACKEND_COMPLETION.md:111`, `MetalDevice.mm:1198` comment | **S** doc | windows-now
- **B-OPENGL** | CLOSE the OpenGL backend (~25%, ~16 kNotImplemented) as permanently out-of-charter (charter is Vulkan→D3D12→Metal). Stop counting it as an "open" backend. | ADR + `src/opengl/` header note | **S** | windows-now
- **B-D3D12-STOREOP** | CLOSE StoreOp/kDontCare as a perf-only hint with NO correctness impact on D3D12 (contents always preserved). Correct the doc claim "symmetric" → "Vulkan honors it, D3D12 ignores it (benign hint)". | ADR + roadmap text | **S** | windows-now
- **B-D3D12-STRUCTURED** | CLOSE kStorageBuffer raw-only: all engine SSBOs are ByteAddress/raw on the HLSL side; a StructuredBuffer<T> path needs a non-existent interface stride field. Promote-on-request when a StructuredBuffer shader appears. | ADR | **S** | windows-now
- **B-D3D12-PARALLEL** | CLOSE D3D12 sequential-replay parallel-pass: correctness + determinism are 100% (byte-identical lane-order replay); only peak replay throughput differs from Vulkan secondaries. Promote-on-request. | ADR (note already in class header) | **S** | windows-now
- **B-D3D12-SYNC** | CLOSE binary-semaphore-inert sync model: D3D12 acquire/submit treat binary semaphores as no-ops (timeline/fence is the real primitive) — internally consistent. Document as an explicit cross-backend sync contract note. | ADR + ICommandBuffer doc | **S** | windows-now
- **B-PLATFORM-GUARD** | CLOSE Vulkan `create_swapchain #else kNotImplemented` — correct-by-design compile-time guard (Win32 path active here), not a runtime hole. One-line comment/ADR. | `VulkanDevice.cpp:2955` | **trivial** | windows-now
- **B-CAP-GATES** | CLOSE the capability-gate kNotImplemented set (RT/mesh/bindless when ext absent) — these are designed feature-gate semantics per IDevice.hpp:93-96, callers branch on features() first. Record in the kNotImplemented inventory (C-INVENTORY). | inventory entry | **trivial** | windows-now
- **B-SPARSE** | CLOSE sparse/tiled resources (vkQueueBindSparse) as a future virtual-texture RFC, not on the current bar. | ADR | **S** | windows-now
- **B-NULL-NONBLOCK-READBACK** | CLOSE async/fenced non-blocking device readback — blocking one-shot exists + tested; cmd-level copy is the building block. Promote-on-request. | ADR | **S** | windows-now

> Decisions that flip to IMPLEMENT in this plan (NOT closed): indirect-draw (A-INDIRECT), AS compaction (A-COMPACTION), AS refit (A-REFIT) — the prior roadmap deferred these "for parity"; since the user now wants nothing open and the host can verify on its RTX 3080, we IMPLEMENT them on all 3 backends rather than close.

---

## GROUP C — TEST (every shipped/fixed feature gets a fail-on-revert test; close cross-backend holes)

- **C-INVENTORY** | Create the single-source-of-truth kNotImplemented inventory table (every site on all 3 backends labelled gate | defer(ADR) | implemented; count of TODO-holes must read 0). | `docs/RHI_KNOTIMPL_INVENTORY.md` | **S** | windows-now
- **C-VK-PARALLEL-PIXEL** | ADD Vulkan serial==parallel pixel-equivalence + lane-order test (D3D12 has `test_d3d12_parallel_pass`; Vulkan has none). | `tests/test_rhi_vulkan_parallel_pixel.cpp` | **M** | windows-now (lavapipe)
- **C-VK-RT-SBT** | ADD Vulkan SBT host-side test (group-handle size/alignment + SBT region math + empty-shader-list rejection) mirroring D3D12's structural half; the reference backend's SBT path is currently untested. Functional dispatch run = nvidia-here. | `tests/test_vk_rt_sbt.cpp` | **M** | windows-now host / nvidia-here dispatch
- **C-VK-VALIDATION-FULL** | ADD a full-frame validation-clean test (render pass + draw + barrier + present + dispatch_rays, assert validation_error_count()==0) and run the broad stress loop with enable_validation=true. | `tests/test_rhi_vulkan.cpp` | **M** | windows-now (needs VK validation layer installed on the lane — operator action)
- **C-VK-TIMELINE** | ADD timeline-gated cross-queue overlap test (async-compute gated on graphics via a timeline value). | `tests/test_rhi_vulkan.cpp` | **S** | windows-now
- **C-D3D12-FIXES** | ADD one focused gtest per A.3 fix (SRV-MS, indexed-BLAS create, HDR colour-space CheckColorSpaceSupport, swapchain-resize/ResizeBuffers, format round-trip over all 43, root-cost overflow rejection), each fail-on-temp-revert. | `tests/test_d3d12_*.cpp` | **M** | windows-now (WARP)
- **C-PARITY-BROADEN** | ADD 3-4 scenes to the Vk-vs-D3D12 pixel-parity (cull=kBack winding, true non-opaque alpha blend, sRGB target, MRT, compute-write-then-sample) — D16 is one narrow scene (cull NONE/opaque/no-MRT/no-MSAA). | `tests/test_backend_pixel_parity.cpp` | **M** | windows-now
- **C-CAPS-CONFORMANCE** | ADD a cross-backend caps-conformance test over all instantiable backends: every limits_ field non-zero/sane, every TRUE feature flag backed by real capability, and PIN the deliberate inconsistencies (Metal no-geometry/no-tess) so they are contract not accident. Resolve the dead `variable_rate_shading` flag (wire via V-FEAT-VRS or remove). | `tests/test_rhi_caps_conformance.cpp` | **M** | windows-now (Vk+D3D12; Metal arm authored)
- **C-LEAK-LIFETIME** | ADD create-every-resource-kind → use → destroy → wait_idle → repeat stress test + negative double-destroy / destroy-swapchain-owned-image safety; run under Windows ASAN-Release (the documented-working config). | `tests/test_rhi_lifetime_stress.cpp` | **M** | windows-now
- **C-FORMAT-MATRIX** | ADD per-backend Format-support conformance (which Formats each backend accepts for sample/RT/storage) so create_texture can't silently diverge. | `tests/test_rhi_format_support.cpp` | **M** | windows-now
- **C-METAL-TIER2** | AUTHOR the 9 Metal GPU test binaries (device/buffer/texture/pipeline/shader/descriptor/barrier/swapchain/rt) behind `#if __APPLE__` (D3D12 tests are the template). Authored windows-now; RUN mac-gated. | `tests/test_metal_*.cpp` | **L** | windows-now author / mac-gated run
- **C-METAL-CULL** | AUTHOR Metal face-cull parity test (port `test_d3d12_face_cull_parity.cpp`) — locks the negative-viewport winding-inversion story. | `tests/test_metal_face_cull_parity.cpp` | **M** | windows-now author / mac-gated run
- **C-TOOLCHAIN-RERUN** | RE-RUN + extend `cd_test_metal_shader_toolchain` (the ONLY windows-now Metal verification gate) — confirm 11/11 still green + binding-index disjointness after M1-M12 churn. | existing test | **S** | windows-now
- **C-DOC-RECONCILE** | Refresh/stamp-historical the stale docs: `D3D12_PARITY_AUDIT.md` (Phase-466 snapshot), `RHI_PARITY_STATUS.md` (says Metal=31 LOC); fix ADR-X6 stale `hello_rt` reference (now hello_path_trace); reconcile the one true RT-architecture statement across all 4 docs. | docs | **S** | windows-now
- **C-READMES** | ADD per-backend READMEs (`src/{vulkan,d3d12,metal}/README.md`): completion status, ADR links, intentional-defer list, remaining HW verification (project's 100%-README norm). | 3 files | **S** | windows-now

---

## GROUP D — HW-VERIFY (GPU verification; the explicit residual)

> Re-scoped vs older audits: the host HAS an RTX 3080, so the Vulkan+D3D12 GPU items are **runnable here NOW** (nvidia-here, not blocked). The ONLY true hard blocker is Metal.

- **D-VK-GOLDEN** | nvidia-here | Capture + commit + gate Vulkan golden images: (1) lavapipe software goldens (30-min operator dispatch already wired in ci.yml, currently self-skips on empty dir); (2) RTX-3080 hardware goldens. Activate the standing pixel-regression gate. | `tests/golden/lavapipe/*.png` + ci.yml
- **D-D3D12-PARITY-RUN** | **nvidia-here (RUN NOW)** | Run `test_backend_pixel_parity` + `test_d3d12_{face_cull,msaa,sampler}_parity` GREEN on this host's RTX 3080 → converts D3D12 from "code-complete" to "GPU-verified". Highest-value autonomous verification available. | host run
- **D-DXR-RUN** | **nvidia-here (RUN NOW)** | Enable `features_.ray_tracing` on the RTX 3080, run the full DXR path (CreateStateObject→DispatchRays) + Vulkan VK_KHR_ray_tracing_pipeline dispatch, diff readback vs golden. Also verifies A-COMPACTION/A-REFIT ray-equivalence. | host run + ci-nvidia-windows.yml (register the self-hosted runner for standing CI)
- **D-VK-VALIDATION-LANE** | windows-now + operator | Install VK_LAYER_KHRONOS_validation on the lavapipe CI lane, export VK_INSTANCE_LAYERS, run C-VK-VALIDATION-FULL as a build-failing gate. | ci.yml
- **D-METAL-COMPILE** | **mac-gated (HARD BLOCKER)** | `CD_RHI_METAL_ENABLED=ON` configure + compile all .mm to -Werror-clean on Apple Clang (never compiled). Prerequisite for ALL Metal GPU work. | Mac/Apple-Silicon runner
- **D-METAL-GPU** | **mac-gated (HARD BLOCKER)** | Run C-METAL-TIER2 (9 binaries) + C-METAL-CULL + windowed hello_metal with MTL validation on; confirm M-caps constants against real device; verify M-MSAA/M-STENCIL/M-DEPTHBIAS/M-HDR-EDR/M-BCN drafts. | Mac + Fork-A sign-off
- **D-METAL-PARITY** | **mac-gated (HARD BLOCKER)** | Capture a Metal golden → add the Vulkan-or-D3D12 vs Metal arm to the cross-backend pixel matrix (currently absent entirely). | Mac runner
- **D-ARGBUF-TIER** | mac-gated | Decide Tier-1 argument-buffer fork: if Apple-Silicon-only (Fork-A), assert Tier2 at create_metal_device + CLOSE-AS-WONTFIX Tier-1; else add Tier-1 fallback. | Mac
- **D-NVIDIA-RUNNER** | operator | Register the PENDING NVIDIA self-hosted runner (ci-nvidia-windows.yml) so RT runs on real hardware on tag/dispatch as a standing gate. | operator action

---

## EXECUTION WAVES

- **Wave 0 (½ day, windows-now)** — Truth + safety net first: C-INVENTORY, C-DOC-RECONCILE, B-RT-SCOPE/B-OPENGL/B-PLATFORM-GUARD/B-CAP-GATES + the 4 trivial D3D12/sync ADRs, C-TOOLCHAIN-RERUN, all 5 Metal windows-now hardening edits (M-EVENT-ATOMIC/M-BCN-PITCH/M-CPP-STUB/M-DOC-PUSHIDX/M-BANNER), V-DBGDEPTH + 3 Vulkan feature flags. → docs are honest, parity-fixes shipped, zero false claims.
- **Wave 1 (1-2 sessions, windows-now)** — D3D12 implement slice: D-SRV-MS, D-BLAS-INDEXED, D-HDR-SWAPCHAIN, D-SWAPCHAIN-RESIZE, D-FORMAT-MAP, D-ROOTCOST, D-MIPSTATE + C-D3D12-FIXES tests.
- **Wave 2 (nvidia-here, RUN NOW)** — D-D3D12-PARITY-RUN + D-DXR-RUN + D-VK-GOLDEN on the RTX 3080. **This is the single highest-value action** — converts D3D12 to GPU-verified and locks Vulkan goldens.
- **Wave 3 (windows-now)** — Cross-cutting subsystems: A-AS-FLAGS → A-COMPACTION + A-REFIT (verify on RTX 3080), A-QUERY, A-INDIRECT, A-BINDPOINT, A-RESULT-DIAG + Vulkan V-MSAA-RESOLVE/V-COPY-IMG/V-PIPECACHE + C-VK-* tests + C-CAPS/C-LEAK/C-FORMAT/C-PARITY-BROADEN tests + V-MULTIVIEW (or close).
- **Wave 4 (windows-now)** — Metal draft + author: M-RT-PIPELINE (or B-RT-SCOPE close) + M-MSAA/M-STENCIL/M-DEPTHBIAS/M-HDR-EDR drafts + C-METAL-TIER2/C-METAL-CULL authored behind `#if __APPLE__` + per-backend READMEs. **At end of Wave 4 = IMPL-100% reached.**
- **Wave 5 (HW residual)** — D-METAL-COMPILE → D-METAL-GPU → D-METAL-PARITY → D-ARGBUF-TIER on a Mac; D-NVIDIA-RUNNER + D-VK-VALIDATION-LANE as standing CI. **This is the only post-IMPL-100% work and it is hardware-gated, not code.**

## Effort / Autonomy summary
- windows-now closable (Waves 0,1,3,4 + authoring): ~46 items. Estimated 6-9 focused sessions.
- nvidia-here, runnable on THIS host now (Wave 2 + A-COMPACTION/REFIT/DXR verify): ~5 items — no new hardware needed.
- mac-gated HARD blockers (Wave 5): 5 items — need a Mac/Apple-Silicon runner the user must provide.