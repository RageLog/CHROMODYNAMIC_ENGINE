# cd::particle::system

CPU-side particle simulation. Designers author `EmitterSpec` /
`ParticleSpec` records (see `cd::asset::vfx_authoring` for the JSON
loader); the runtime ticks emitters via `System::tick(dt)` and
publishes a renderer-facing `ParticleSnapshot`.

**Naming note:** the library was renamed from `particle_system_v2`
in M5 — no v1 ever existed; the `_v2` suffix was a misleading carry-
over from a planning draft. The current canonical name is
`cd::particle::system`.

## Design

* **SoA storage** — parallel `std::vector<float>` rows for
  `position_x / position_y / position_z / velocity_x / ...`. Cache-
  friendly for the tick loop; swap-and-pop deletion keeps gaps out
  of hot iteration.
* **Integration** — semi-implicit Euler (velocity first, position
  second). Stable under typical gameplay step sizes (1/60 s — 1/120 s)
  for the gravitated-particle case.
* **Spawn accumulator** — fractional particles carry over between
  ticks so a 2.5 particles/frame emission rate emits 3 then 2 then 3,
  not 2 every frame.
* **Random sampling** — `std::mt19937` only. No `cd::math` dep — the
  particle library is `cd::core` plus stdlib.

GPU dispatch is the V3 roadmap and is **out of scope** here — V3
will land as a sibling library, not a rewrite of this one.

## Public surface

```cpp
namespace cd::particle::system {

struct ParticleSpec
{
    float           lifetime_seconds;
    cd::math::Vec3f initial_velocity;
    cd::math::Vec3f gravity;
    float           size_start, size_end;
    cd::math::Vec3f colour_start, colour_end;
    float           alpha_start, alpha_end;
};

struct EmitterSpec
{
    ParticleSpec    particle;
    float           rate_per_second;
    float           lifetime_seconds;       // 0 = infinite emitter
};

using EmitterId = uint32_t;

class System
{
public:
    EmitterId                              add_emitter(EmitterSpec);
    void                                   remove_emitter(EmitterId);
    void                                   tick(float dt);

    [[nodiscard]] ParticleSnapshot         snapshot() const;
};

struct ParticleSnapshot
{
    std::span<const float>     position_x, position_y, position_z;
    std::span<const float>     size;
    std::span<const float>     colour_r, colour_g, colour_b, alpha;
};

}
```

## Tick contract

`tick(dt)`:

1. Apply gravity + velocity to every live particle.
2. Apply velocity to position.
3. Decrement `lifetime_remaining_seconds`; swap-and-pop dead
   particles.
4. For each emitter:
   1. Add `rate_per_second × dt` to its spawn accumulator.
   2. While the accumulator ≥ 1, spawn a particle and decrement by 1.
   3. If emitter is not infinite, decrement its
      `lifetime_remaining_seconds` and remove on expiry.

The order is fixed so an external job can `tick()` the system once
per frame and rendering reads `snapshot()` afterwards with no race.

## Renderer wiring

`ParticleSnapshot` is **read-only**, typed as `std::span<const float>`
to keep the renderer free of any allocator coupling. The renderer
binds the spans to a per-frame instance buffer (see
`cd::render::gpu_particles` for the GPU side).

## Dependencies

* `cd::core` — `Defines.hpp` (PUBLIC, header use).
