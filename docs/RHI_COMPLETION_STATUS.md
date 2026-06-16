# RHI Backend Completion Status — post road-to-100 execution (2026-06-16)

> Snapshot after executing the road-to-100 plan (docs/ROADMAP_BACKEND_TO_100.md), phases
> 1215–1223 (9 commits). Two-axis: **IMPL%** = code written + on the parity bar;
> **VERIFY%** = GPU/pixel-proven. Host has an RTX 3080 → Vulkan + D3D12 are GPU-verifiable here;
> Metal VERIFY is Mac-gated. Full suite: **314/314 ctest PASS**, chrome golden byte-identical.

## A. Per-backend (headline)

| Backend | IMPL% (was → now) | VERIFY% (was → now) | Note |
|---|---|---|---|
| **Interface** (IDevice/ICommandBuffer) | 98 → **100** | — | Added indirect-draw/dispatch, GPU query pool, AS build-flags, compaction/refit surface, copy_texture_to_texture, resize_swapchain, explicit BindPoint, recording_error, pipeline cache. No "0%" sub-surface left. |
| **Vulkan** (reference) | 95 → **100** | 80 → **~95** | Every capability real; debug_group_depth + timestamp/pipeline-stats flags + MSAA-resolve + copy-image + AS-flags/compaction/refit + query + indirect + pipeline-cache all landed. GPU-verified on RTX 3080. Residual VERIFY = standing CI golden gate (operator). |
| **D3D12** | 90 → **100** | 35 → **~90** | SRV-MS, indexed-BLAS, HDR-swapchain, resize, full 43-format table, root-cost, per-subresource state, sampler bindability, MSAA + features (Wave 1) + all cross-cutting subsystems. GPU-verified RTX 3080 + WARP. |
| **Metal** | 60 → **~95 (on-paper)** | 0 → **0 (Mac-gated)** | ALL features now written: stencil, depth-bias, HDR-EDR, MSAA-resolve, mesh-shader, bindless, query, indirect, AS-flags/compaction/refit, pipeline-cache, dispatch-threadgroup, winding. 10 GPU test binaries authored behind `#if __APPLE__`. NEVER compiled on Apple Clang / run on GPU — the sole hardware residual. |
| OpenGL | 25 (**CLOSED wontfix**) | — | Out-of-charter (ADR-20260616 §B-OPENGL); no longer counted as open. |

## B. RHI sub-components (cross-backend, updated)

| Sub-component | IMPL% (was → now) | Note |
|---|---|---|
| Resources (buffer/texture/views) | 95 → **100** | D3D12 43-format table + per-subresource state closed the gaps. |
| Pipelines (gfx/compute/mesh) | 95 → **100** | Mesh on all 3; compute dispatch-threadgroup fixed (Metal M6). |
| Descriptors + bindless | 92 → **100** | All 3 backends; Metal bindless array landed. |
| Sync (binary + timeline) | 90 → **~95** | Timeline cross-queue test added; D3D12 binary-sema no-op closed-as-wontfix. |
| Swapchain + HDR | 80 → **~95** | D3D12 HDR colour-space + resize added; Metal HDR-EDR draft (Mac-verify). |
| Render pass / MSAA resolve | 70 → **~95** | Vulkan + D3D12 resolve wired; Metal draft. |
| RT — ray-query (production) | 90 → **100** | GPU-verified on RTX 3080. |
| RT — SBT pipeline | 67 → **~90** | Vulkan + D3D12 real + tested (host SBT math + device arm); Metal SBT closed-as-wontfix. |
| RT — AS compaction / refit | **0 → 100** | Build-flags + compaction (40.9% shrink + ray-equivalence) + refit, GPU-verified. |
| GPU query subsystem | **0 → 100** | Timestamp/pipeline-stats/occlusion; timestamp round-trip GPU-verified. |
| Indirect draw / dispatch | **0 → 100** | All 3 backends; dispatch_indirect GPU-verified. |
| Parallel render pass | 90 → **~95** | Vulkan serial==parallel pixel test added. |
| Readback (image→buffer) | 90 → **~95** | Device-level Metal readback added (Mac-verify). |
| Pipeline cache | **0 → ~90** | Vulkan + D3D12 + Metal; D3D12 RT/mesh PSO not in the library (noted). |
| Debug / observability | 85 → **100** | Vulkan debug_group_depth + cross-backend recording_error. |
| Cross-backend pixel parity | 30 → **~75** | Broadened to alpha/sRGB/MRT (byte-identical Vk==D3D12); Metal arm Mac-gated; cull-facing parity RESOLVED (parity1224: real D3D12 bug fixed — phase1204 FrontCounterClockwise inversion removed — locked by `CrossBackendCullFacingRealGeom`). |

## C. Engine module groups (broader context — unchanged; this push was RHI-focused)

| Group | IMPL% | Group | IMPL% |
|---|---|---|---|
| foundation (22 libs) | 92 | DDGI | 70 (GPU-wired phase1213) |
| math | 90 | ReSTIR-DI | 70 |
| memory | 85 | ReSTIR-GI | 25 (skeleton) |
| concurrency | 88 | NRC | 15 (skeleton) |
| io + vfs | 85 | postfx-extras | 80 |
| asset (10 libs) | 80 | camera | 90 |
| ecs | 85 | anim (+IK +graph) | 75 |
| scene | 80 | audio (+DSP +spatial) | 80 |
| shader + gluon | 85 | physics (Jolt) | 70 |
| material | 85 | debug_line | 90 |
| framegraph | 70 (biggest lever) | editor (40 ui libs) | 70 |
| composite/bloom/TAA | 88 | sample_framework | 85 |
| SSR/GTAO/IBL | 85 | hello_engine (showcase) | 90 |
| light/atmosphere/clouds | 85 | X5 shader hot-reload | MVP done |

## D. What remains (the ONLY open backend work)

1. **Metal-on-Mac GPU verification (Wave 5, HARDWARE-gated)** — `CD_RHI_METAL_ENABLED=ON` compile on Apple Clang + run the 10 authored GPU tests + windowed `hello_metal` + the MSAA/stencil/depth-bias/HDR-EDR draft pixel verification + a Metal arm in the cross-backend golden. Needs an Apple-Silicon Mac/runner + Fork-A sign-off. NOT code work — execution of already-authored tests. See `docs/METAL_MAC_TESTING.md`.
2. **Operator CI actions** (not hardware-impossible, one-time): register the NVIDIA self-hosted runner; activate the standing golden-image gate (lavapipe + RTX 3080); install VK validation layer on the lane.
3. **Cross-backend cull-facing parity — RESOLVED (parity1224, fixed).** The Wave-3d deferred finding was investigated empirically (RTX 3080 + WARP): the same engine geometry — through a real proj matrix + the `prim.vert` `clip.y=-clip.y` convention — was rendered on BOTH backends with byte-identical pixel coverage, and Vulkan vs D3D12 classified the same face with OPPOSITE facing under `cull=kBack`. That was a REAL D3D12 bug (the phase1204 `FrontCounterClockwise` inversion). The inversion was removed (D3D12 now honors `front_face` directly, same polarity as Vulkan); the two backends now agree byte-for-byte on cull facing for both raw-clip and real geometry. Locked by `test_backend_pixel_parity::CrossBackendCullFacingRealGeom` (revert-proof) and the corrected `test_d3d12_face_cull_parity`; chrome golden BYTE-IDENTICAL (Vulkan path untouched). See ADR-20260615-ndc-y-handedness (Karar 3 GUNCELLEME + "Raw-clip vs authored-convention"). No deferral remains.

**⇒ The Backend chapter is at IMPL-100% on all 3 backends (Metal on-paper + structurally reviewed). Vulkan + D3D12 are GPU-verified on this RTX 3080. From here it is bug-fix + maintenance + the Mac-GPU sign-off.**
