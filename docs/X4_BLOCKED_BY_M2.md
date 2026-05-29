# X4 D3D12 Parity -- Blocked on M2 sample_framework

**Status**: BLOCKED.  Authored 2026-05-29 during Marathon Run 31.

**Decision**: Do not start the full M4 sprint until M2
sample_framework has landed.  The X4 substantive piece (backend
parity smoke + audit ADR) is already shipped in Run 28 / phase368.
The follow-up `hello_d3d12_pbr` sample with golden-image diff is
the M4H/M4I sub-phase of the original M4 plan, and that plan is
explicitly conditional on M2.

## Why

Per `docs/ADR/ADR-20260529-M4-d3d12-parity.md` (section
"Pre-condition: M2 landed"):

> M4H ships `hello_d3d12_pbr` by deriving from `cd::sample::App`
> with `AppConfig::backend = kD3D12`.  Without M2 the alternative
> is to duplicate 2143 lines of hello_engine main() body, which
> is wasteful + duplicates a future debt.  So M4 starts only
> after M2 lands; M4A-M4G can run before in parallel with M2.

Starting M4H/M4I now would force a choice between:

1. Duplicate ~2000 lines of `hello_engine` main() into
   `samples/render/hello_d3d12_pbr/main.cpp`.  Per the project's
   "DON'T regenerate IBL bake parameters" rule and the marathon
   "scope DOWN, ship the slice that works" rule, duplicating
   that much code as a transitional artefact is the wrong
   trade-off -- it would have to be retired the moment M2 lands.

2. Start writing `cd::sample::App` from scratch as part of this
   run.  This conflicts directly with background workflow
   `wf_6bfce98c-00f` (phases 356-358 NXZ in flight), which the
   user's brief explicitly fenced off ("Stay clear of:
   `engine/world/sample_framework/**`").

Both alternatives are net-negative for the codebase.  The
honest call is "wait until M2 lands, then unblock M4H/M4I".

## What WAS shipped this Path-A run

- **Run 29** / phase369 (X6B):  recursive RT shading in
  `hello_rt` -- closest-hit fires `traceRayEXT` with payload
  depth tracking; miss returns a procedural sky; pipeline
  `max_recursion` lifted from 1 to 2; 4 new descriptor smoke
  tests lock the recursive surface.  RTX 3080 Laptop GPU
  dispatches clean; ADR-X6 follow-up #1 marked DONE.

- **Run 30** / phase370 (X7B):  archetype-table side layer
  `cd::ecs::ArchetypeWorld` next to (not replacing) the
  sparse-set `cd::ecs::World`.  7-case gtest proof + 1000-
  entity microbench showing archetype ~9.3x faster than
  sparse-set on the dense-row `each<Position, Velocity>`
  query.  ADR-X7 rejected-alternative "side layer without
  disturbing SparseSet" marked REALISED.

- **Run 31** (this doc):  X4 blocked, queued.

## What X4 needs to unblock

When background workflow `wf_6bfce98c-00f` lands phases
356-358 (M2A/M2B/M2C/M2D/M2E), the following becomes
buildable in a single subsequent Run:

- `cd::sample::App` exists with `AppConfig::backend` enum
  supporting `kVulkan` + `kD3D12`.
- `hello_engine` is ported to derive from `cd::sample::App`
  with `backend = kVulkan`.
- A new `samples/render/hello_d3d12_pbr/main.cpp` can be ~200
  lines deriving from the same `cd::sample::App` with
  `backend = kD3D12`, reusing the existing `HelloMaterials`
  hpp.
- `cd_test_render_parity_d3d12` can capture first-frame
  backbuffers from both samples and compare via `cd::imgdiff`
  (FLIP perceptual diff, threshold 0.01 mean / 0.05 P99).

The M4A-M4G sub-phases (closing the remaining
`kNotImplemented` sites in `D3D12Device.cpp` -- 1D textures,
3D textures, full SubmitDesc, DXR pipeline state object) can
in principle run in parallel with M2 according to the M4 ADR,
but starting them in this Run would create a multi-week branch
that risks colliding with whatever the M2 workflow ends up
changing in the descriptor / handle surface.  Conservative
call: also queue M4A-M4G to the post-M2 sprint.

## Queue for next sprint

When M2 ships:

1. M4A-M4G:  close the 4-5 `kNotImplemented` sites in
   `engine/render/rhi_d3d12/src/D3D12Device.cpp` (1D / 3D
   textures, full submit semantics, DXR PSO, full
   descriptor-update breadth).  3-4 weeks per the M4 plan.

2. M4H:  port the PBR shader stack to native DXIL
   (DirectXShaderCompiler) and build
   `samples/render/hello_d3d12_pbr` deriving from
   `cd::sample::App`.

3. M4I:  `cd_test_render_parity_d3d12` golden-image gate.

## Pointers

- `docs/ADR/ADR-20260529-X4-d3d12-parity-status.md` -- the
  Run 28 audit ADR.
- `docs/ADR/ADR-20260529-M4-d3d12-parity.md` -- the original
  M-tier milestone plan (M4A-M4I sub-phases).
- `docs/ADR/ADR-20260529-M2-sample-framework.md` -- the M2
  pre-condition; once landed this doc can be deleted.
- `engine/render/rhi_d3d12/src/D3D12Device.cpp` -- 2906 lines
  of substantive backend; X4 audit confirmed parity ratio
  ~73% vs rhi_vulkan.
- `engine/render/rhi/tests/test_backend_parity.cpp` -- 8-case
  Run 28 smoke gating the cross-backend ABI surface.
