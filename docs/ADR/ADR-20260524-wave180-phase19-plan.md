# ADR — Wave 180 — Phase 19 plan

- Date: 2026-05-24
- Status: Accepted
- Wave: 180
- Predecessor: Phase 18 (wave178) closed at v0.49.0.

## Context

Phase 19 catches up four items deferred from Phase 18 (Vulkan TLAS,
AMD CI workflow, editor selection outline, job priority benchmark)
plus introduces three new candidates that mature alongside the
Phase-13-onward marathon work: particle system primitive, animation
blending, scene streaming basics.

## Sub-phases

### 19.A — Vulkan TLAS construction
Mirror 17.A (BLAS) for the top-level case. Adds
`AccelStructureKind::kTopLevel` real path with instance descriptor
buffer.

### 19.B — AMD CI workflow placeholder
GitHub Actions workflow_dispatch entry that documents the Mesa/RADV
test path. Dispatch-only; no runner yet.

### 19.C — Editor selection outline (visible highlight)
Re-render the currently-selected entity with `cull=kFront` and a
1.05× model-matrix scale to produce a flat-color silhouette.

### 19.D — Job system priority benchmark extension
hello_bench gains a "priority throughput" suite that times
WorkStealingThreadPool submissions at three priority levels.

### 19.E — Particle system primitive (header-only)
`cd::scene::ParticleSystem` — fixed-pool emitter + per-particle
position/velocity/lifetime. Headless test pins integration math.

### 19.F — Animation skeletal blend primitive (header-only)
`cd::anim::BlendTree2` — two-pose linear blend (the "walk ↔ run"
case). Headless test verifies pose interpolation.

### 19.G — Scene LOD primitive (header-only)
`cd::scene::LodSelector` — per-entity distance threshold table;
selects the active LOD index based on camera-distance.

## Tag

Single v0.50.0 at Phase 19 close. v0.50.0 is the halfway-to-v1.0
milestone — psychologically meaningful even though the v1.0 gate
remains the user's call per the rollback ADR.

## References

- ADR-20260524-wave178-phase18-plan.md (deferred items)
- ADR-20260523-wave125 (v1.0 rollback discipline)
