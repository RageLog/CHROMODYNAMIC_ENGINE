# ADR — Wave 190 — Phase 24 plan

- Date: 2026-05-24
- Wave: 190
- Predecessor: Phase 23 closed at v0.54.0.

## Sub-phases

### 24.A — `cd::math::Random` (PCG32 PRNG)
Header-only, deterministic, fast. Foundation for any
randomized sample.

### 24.B — `cd::math::SmoothingFilter` (EMA + linear)
Exponential moving average smoother used by editor camera
inputs / mouse-look damping.

### 24.C — `cd::physics::Obb` (oriented bounding box)
Phase 23.E deferred item. AABB + 3x3 rotation matrix.
Closest-point + sphere/OBB intersection.

### 24.D — `hello_particles` headless sample
Exercises `cd::scene::ParticleSystem` (Phase 19.E) with
gravity. Smoke test for the marathon-shipped primitive.

### 24.E — `hello_bezier` headless sample
Demonstrates `cd::math::CubicBezier` (Phase 22.D) sampling
+ arc length.

## Tag

v0.55.0 at close.
