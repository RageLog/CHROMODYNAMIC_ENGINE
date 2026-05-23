# ADR — Wave 184 — Phase 21 plan

- Date: 2026-05-24
- Status: Accepted
- Wave: 184
- Predecessor: Phase 20 (wave182) closed at v0.51.0.

## Sub-phases (header-only primitives wave)

### 21.A — `cd::physics::Sphere` + intersection helpers
Header-only sphere shape. `intersects(Sphere, Sphere)`,
`contains(Sphere, point)`, `intersects(Sphere, Aabb)`.

### 21.B — `cd::core::SmallVector<T, N>`
Header-only small-buffer-optimized vector that holds up to N
elements inline before heap allocation. Foundation primitive
for the common "few-element vector" hot path in cd::ecs +
cd::scene without dragging in Boost.

### 21.C — `cd::scene::SpatialHash<T>`
Uniform-grid spatial hash for fast neighbour queries. Stores
arbitrary T (typically Entity or std::uint32_t) at world-
space cell coordinates. Headless test.

### 21.D — `cd::input::Axis` abstraction
Multi-source input axis (key pair / mouse motion / gamepad
analog stub) that produces a normalized [-1, 1] float per
frame. Foundation for the editor's WASD + future gamepad
parity.

### 21.E — `cd::math::ease` curve helpers
`linear`, `smoothstep`, `ease_in_quad`, `ease_out_quad`,
`ease_in_out_cubic` — the standard easing kit every UI / VFX
layer eventually needs.

## Tag

v0.52.0 at close.

## References

- ADR-20260524-wave182-phase20-plan.md
- engine/world/physics/include/cd/physics/Aabb.hpp (20.B
  predecessor for 21.A)
