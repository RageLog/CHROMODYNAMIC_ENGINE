# ADR — Wave 186 — Phase 22 plan

- Date: 2026-05-24
- Status: Accepted
- Wave: 186
- Predecessor: Phase 21 (wave184) closed at v0.52.0.

## Sub-phases (continuation of the header-only primitive sweep)

### 22.A — `cd::scene::SpatialHash<T>`
Uniform-grid hash for fast spatial neighbour queries.

### 22.B — `cd::input::Axis`
Multi-source normalized input axis ([-1, 1]) from key pair
and/or future gamepad analog stub.

### 22.C — `cd::physics::ray_intersects_aabb` + Ray primitive
Ray = origin + direction. AABB hit returns t-min.

### 22.D — `cd::math::CubicBezier`
4-control-point Bezier curve evaluator + length approximation.

### 22.E — `cd::core::FixedString<N>` (compile-time string)
Header-only fixed-capacity string for constexpr / non-allocating
contexts.

## Tag

v0.53.0 at close.
