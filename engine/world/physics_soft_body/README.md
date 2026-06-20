# cd::physics::soft_body

CPU **mass-spring / position-based-dynamics (PBD) soft-body** simulation for
cloth and rope. Independent deformable-body system; does not share the
rigid-body `IPhysicsWorld` abstraction (rigid bodies live in
`cd::physics_jolt`).

## Sprints

| Sprint | Phase | Status | Surface |
|--------|-------|--------|---------|
| 1 | `phase721` | shipped | Verlet integration + PBD distance constraints + pin / damping / `apply_force`. |
| 2 | `phase760` | shipped | Self-collision: uniform spatial-hash broad-phase + PBD repulsion near-phase. |
| 2.5 | depth pass | shipped | Provot bending constraints + half-space ground-plane collision (+ Coulomb friction). |
| 3 | sealed | deferred | GPU compute port (Vulkan/D3D12) and FEM / continuum volumetric soft bodies — see *Sealed scope*. |

## Algorithm

* **Verlet integration** — each `Particle` carries `position` and
  `prev_position`; velocity is implicit (`pos − prev_pos`). The per-tick step is

  ```text
  vel       = (pos - prev_pos) * (1 - damping)          // Jakobsen velocity damping
  accel     = gravity + force_accum * inv_mass
  new_pos   = pos + vel + accel * dt^2
  prev_pos  = pos
  pos       = new_pos
  ```

  (See the `text` block above for the per-tick step.) Robust under stiff
  constraints. With `damping == 0` and `prev == pos` the discrete free-fall
  displacement after *n* ticks is exactly `a · dt² · n(n+1)/2` (the discrete
  Verlet sum, verified analytically in
  `SoftBodyIntegrator.SingleParticleFreeFallAnalytic`).

* **PBD distance constraints** (Müller 2006) — each `SpringConstraint`
  projects the pair back onto its `rest_length`:

  ```text
  d      = p[b] - p[a]
  error  = ||d|| - rest_length
  lambda = stiffness * error / ((w_a + w_b) * ||d||)
  p[a] += w_a * lambda * d
  p[b] -= w_b * lambda * d        (w = inv_mass)
  ```

  Iterated `solver_iterations` times per tick (Gauss-Seidel). `stiffness ∈ [0,1]`
  scales the per-iteration correction; convergence improves with iteration count
  (4–8 typically suffice for cloth, 10–30 for stiff rope).

* **Bending constraints** (Provot 1995, flexion springs) — a `BendingConstraint`
  is the *same* PBD distance projection over a longer span (typically `i..i+2`)
  with an independent stiffness, so the sheet resists folding without affecting
  its stretch behaviour. Empty `bending_constraints` ⇒ zero cost.

* **Self-collision** (Teschner 2003) — particles are binned into a uniform
  spatial hash; each particle visits its 3×3×3 cell neighbourhood and any pair
  closer than `2 · particle_radius` is pushed apart by a PBD repulsion impulse.
  Disabled by default (`enable_self_collision == false`).

* **Ground plane** — a half-space (infinite plane) `n̂ · x ≥ offset`. After each
  iteration any penetrating non-static particle is projected back onto the plane
  along `n̂`; optional Coulomb friction damps the tangential Verlet velocity.
  Disabled by default (`enable_ground == false`). Static / pinned particles are
  immovable obstacles.

The solver is **not** XPBD (no Lagrangian multipliers, no compliance term).
Spring elasticity lives in the `stiffness` factor and the iteration count rather
than a continuous physical compliance — a deliberate simplification that keeps
the inner loop tight.

## Public surface

```cpp
namespace cd::physics::soft_body {

struct Particle           { std::array<float,3> position, prev_position;
                            float inv_mass; bool pinned; };          // 0 / pinned = static
struct SpringConstraint   { uint32_t a, b; float rest_length, stiffness; };
struct BendingConstraint  { uint32_t a, b; float bend_rest_length, stiffness; };
struct SelfCollision      { bool enable_self_collision; float particle_radius,
                            spatial_hash_cell_size; };
struct GroundPlane        { bool enable_ground; std::array<float,3> normal;
                            float offset, friction; };
struct SoftBodyConfig     { std::vector<Particle> particles;
                            std::vector<SpringConstraint> constraints;
                            std::vector<BendingConstraint> bending_constraints;
                            uint32_t solver_iterations; float damping;
                            SelfCollision self_collision; GroundPlane ground; };

class SoftBody {
public:
    void configure(const SoftBodyConfig&) noexcept;
    void tick(float dt, std::array<float,3> gravity) noexcept;      // dt<=0 is a no-op
    [[nodiscard]] std::span<const Particle> particles() const noexcept;
    void apply_force(uint32_t particle_idx, std::array<float,3> force) noexcept;
};

}
```

`apply_force` injects a one-shot impulse via the force accumulator (converted to
a `prev_position`/acceleration shift in the Verlet step), matching the canonical
Verlet "fake force" pattern. Out-of-range indices are silently ignored.

## Numerical safety

* `dt <= 0` ⇒ no-op; empty body ⇒ no-op.
* Coincident pair (`dist² < 1e-12`) skips the projection (no divide-by-zero).
* `w_sum == 0` (both particles static) ⇒ no correction.
* Zero / degenerate ground normal ⇒ ground pass is a no-op.
* `inv_mass == 0` or `pinned == true` ⇒ particle is static; restored to its
  configured position after every iteration.

## Sealed scope (Sprint 3)

A GPU compute (XPBD) port and a co-rotational **FEM / continuum volumetric**
solver (tetrahedral, with self-collision via signed-distance fields) are
multi-week efforts outside this library's mass-spring/PBD charter. They are
intentionally **not** implemented; the CPU mass-spring + PBD path here is
complete and fully tested.

## Dependencies

* `cd::core` — base defines.

No `cd::math` link (raw `std::array<float,3>` internally). No `cd::physics`
link — soft bodies are not rigid bodies. No `cd::physics_jolt` — Jolt does not
currently expose the soft-body APIs we want to depend on.

## Tests

`tests/test_soft_body.cpp` — 29 gtest cases (AAA, deterministic, no `sleep_for`):
integrator analytic free-fall + damping-energy + gravity-sign + empty/zero
guards; distance over-stretch / compression / zero-stiffness / zero-rest-length;
bending fold-resistance + empty-baseline-identity + invalid-index; ground
stop/fall-through/zero-normal/friction/pinned-obstacle; self-collision
single-particle + zero-radius edge cases; plus the original Sprint-1/2 rope,
pin, iteration-convergence, and perf-smoke cases.
