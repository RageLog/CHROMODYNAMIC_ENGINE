# cd::physics::soft_body

CPU **mass-spring soft-body** simulation for cloth and rope. Independent
deformable-body system; does not share the rigid-body `IPhysicsWorld`
abstraction (rigid bodies live in `cd::physics_jolt`).

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase721` | Verlet integration + PBD constraint solver + pin / damping / apply_force. |
| 2 | deferred  | Self-collision detection + spatial hashing. |
| 3 | deferred  | GPU compute port (Vulkan / D3D12 compute shader). |

## Algorithm

* **Verlet integration** — each `Particle` carries `position` and
  `prev_position`; velocity is implicit (`pos − prev_pos`). Robust
  under stiff constraints.
* **Position-based dynamics (PBD)** — `SpringConstraint`s clamp the
  distance between two particle indices. The solver iterates `N_iter`
  passes per tick, each pass walks every constraint and projects the
  pair back onto the rest-length. Convergence improves with iteration
  count; 4–8 iterations typically suffice for cloth.

The solver is **not** XPBD (no Lagrangian multipliers, no compliance
term yet). Spring stiffness lives in the integer iteration count, not
in a continuous parameter — a deliberate Sprint-1 simplification that
keeps the inner loop tight.

## Public surface

```cpp
namespace cd::physics::soft_body {

struct Particle
{
    cd::math::Vec3f  position;
    cd::math::Vec3f  prev_position;
    float            inv_mass;          // 0 = pinned
};

struct SpringConstraint
{
    uint32_t  i, j;
    float     rest_length;
};

struct SoftBodyConfig
{
    cd::math::Vec3f  gravity        { 0.0F, -9.81F, 0.0F };
    float            damping        { 0.001F };
    uint32_t         solver_iters   { 4 };
};

class SoftBody
{
public:
    void                                  set_config(SoftBodyConfig);

    uint32_t                              add_particle(cd::math::Vec3f pos,
                                                       float inv_mass);
    void                                  add_constraint(SpringConstraint);
    void                                  pin(uint32_t particle_index);
    void                                  apply_force(uint32_t particle_index,
                                                       cd::math::Vec3f F,
                                                       float dt);

    void                                  tick(float dt);

    [[nodiscard]] std::span<const Particle>  particles() const noexcept;
};

}
```

## Tick contract

```
  1. Integrate (Verlet):
       new_pos = pos + (pos - prev_pos) * (1 - damping) + gravity * dt^2
       prev_pos = pos
       pos      = new_pos
  2. For solver_iters passes:
       For each spring (i, j):
         d = pos[j] - pos[i]
         correction = (length(d) - rest_length) / 2 * normalize(d)
         pos[i] +=  correction
         pos[j] -=  correction
       Pinned particles (inv_mass == 0) are skipped during correction.
```

`apply_force` injects an impulse by adjusting `prev_position` —
matches the canonical Verlet "fake force" pattern.

## Sprint-2 self-collision plan

The cloth-on-cloth case (folded curtain, draped flag overlapping
itself) needs particle-particle collision detection. Plan:

* Uniform spatial hash with cell size = 2 × average rest-length.
* Per-pair distance < 2r repulsion impulse.
* Tied into the same iteration loop as the constraint solver.

## Dependencies

* `cd::core` — base defines, `CD_NODISCARD`.

No `cd::math` link (uses raw float arrays internally; the public
`Vec3f` is a header-only POD). No `cd::physics` link — soft bodies
are not rigid bodies. No `cd::physics_jolt` — Jolt does not currently
expose soft-body APIs we want to depend on.
