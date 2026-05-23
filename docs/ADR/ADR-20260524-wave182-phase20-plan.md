# ADR — Wave 182 — Phase 20 plan

- Date: 2026-05-24
- Status: Accepted
- Wave: 182
- Predecessor: Phase 19 (wave180) closed at v0.50.0.

## Sub-phases

### 20.A — AMD lavapipe-style Mesa CI workflow_dispatch placeholder
Workflow file `linux-vulkan-amd-mesa.yml` mirroring the Phase
13.B lavapipe job pattern. Dispatch-only; documents the AMD
Mesa/RADV smoke path.

### 20.B — AABB collision primitive (`cd::physics::aabb_overlaps`)
Header-only axis-aligned bounding-box overlap test. The simplest
broad-phase primitive every physics layer wants before Jolt
integration lands. Headless tests.

### 20.C — Editor selection outline (silhouette pass)
Re-render the selected cube with `cull = kFront` + scale 1.05.
Uses the existing material; no new shader. Adds a tiny piece of
viewport feedback.

### 20.D — `cd::scene::Frustum::contains_aabb` test extension
The Frustum primitive already exists for camera culling; add a
short-list of regression tests against degenerate frustums +
edge-case AABBs.

### 20.E — `cd::concurrency::Stopwatch` (header-only)
Marathon code uses `std::chrono::steady_clock` everywhere; a
named primitive standardizes "start / restart / elapsed_ms"
across samples + tests.

### 20.F — Particle system gravity sample
hello_particles sample exercises Phase 19's ParticleSystem with a
gravity field. Headless smoke (counts living particles per tick).

## Tag

Single tag v0.51.0 at Phase 20 close.

## References

- ADR-20260524-wave180-phase19-plan.md
- engine/world/scene/include/cd/scene/ParticleSystem.hpp
  (consumer for 20.F)
