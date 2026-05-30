# cd::game::particles_event

**Purpose**: Recipe-driven gameplay particle dispatcher. Phase 498 / G3.4 in the Phase-G gameplay tier.

**Namespace**: `cd::game::particles_event`.

**Header**: `cd/game/particles_event/ParticlesEvent.hpp`.

**Scope (intentionally narrow)**:

* Stage authored named "recipes" mapped to bursts of particles.
* Manage a list of `ActiveBurst` records with deterministic indices.
* Age every burst per `tick(dt)`, decay `alive_count` linearly, drop expired bursts.
* Fire user-supplied `on_emit` callbacks once per `fire()` call.

The dispatcher does **not** simulate per-particle physics, does **not** touch any RHI / shader / framegraph resource, and does **not** link against `cd::render::gpu_particles`. The GPU sink is one tier above this library: a render-side adapter reads `active_bursts()` each frame and translates each `ActiveBurst` into the GPU sink's spawn / advance commands.

**Primary types**:

| Type | Role |
|---|---|
| `ParticleRecipe` | Authored data: `name`, `count`, `lifetime_s`, `gravity`, `velocity_min/max`, `color_start/end`, `emitter_shape`, `emitter_radius_or_extent`. |
| `ActiveBurst` | Live burst: `recipe_idx`, `origin`, `rotation`, `age_s`, `alive_count`. |
| `EmitterShape` | `kSphere` / `kCone` / `kBox`. |
| `ParticleEventDispatcher` | Registry + active-burst manager. |

**Emitter shape parameter encoding** (`ParticleRecipe::emitter_radius_or_extent`):

* `kSphere`: radius scalar packed into `.x`; `.y` / `.z` unused.
* `kCone`  : half-angle (radians) in `.x`, height in `.y`; `.z` reserved.
* `kBox`   : full Vec3 half-extents (x, y, z).

The render-side adapter is responsible for sampling spawn positions inside the shape and clamping per-particle velocities into the `velocity_min` / `velocity_max` envelope; this library stages the parameters only.

**Lifecycle**:

1. `register_recipe(name, recipe)` - stores or replaces a recipe under `name`. Existing live bursts that hold the same recipe index continue simulating against the new data.
2. `fire(name, world_position, world_rotation)` - pushes an `ActiveBurst` and invokes every registered `on_emit` callback. Unknown name -> `CD_LOG_WARN` + returns `kInvalidBurst`.
3. `tick(dt)` - ages each burst by `dt`, decays `alive_count` linearly with remaining-life fraction, compacts (stable) finished bursts out. `dt <= 0` is a clean no-op so callers may pause without re-checking.
4. `unregister_recipe(name)` - removes the name -> id map entry. Recipe storage slot is **not** erased so already-spawned bursts keep their valid index. Future fires of that name fail.

**Usage example**:

```cpp
#include <cd/game/particles_event/ParticlesEvent.hpp>

using namespace cd::game::particles_event;

ParticleEventDispatcher dispatcher;

ParticleRecipe muzzle {};
muzzle.count                    = 64U;
muzzle.lifetime_s               = 0.25F;
muzzle.gravity                  = cd::math::Vec3f {0.0F, -9.81F, 0.0F};
muzzle.velocity_min             = cd::math::Vec3f {-2.0F,  0.0F, -2.0F};
muzzle.velocity_max             = cd::math::Vec3f { 2.0F,  6.0F,  2.0F};
muzzle.color_start              = cd::math::Vec4f {1.0F, 0.85F, 0.4F, 1.0F};
muzzle.color_end                = cd::math::Vec4f {1.0F, 0.30F, 0.0F, 0.0F};
muzzle.emitter_shape            = EmitterShape::kCone;
muzzle.emitter_radius_or_extent = cd::math::Vec3f {0.35F, 0.5F, 0.0F};
dispatcher.register_recipe("muzzle_flash", muzzle);

dispatcher.add_on_emit([](const ActiveBurst& b) {
    // play sound, register VFX with the render adapter, etc.
});

// On gameplay event:
dispatcher.fire("muzzle_flash", world_pos, world_rot);

// Each gameplay tick:
dispatcher.tick(dt);

// Render-side adapter (one tier above this library):
for (const ActiveBurst& b : dispatcher.active_bursts())
{
    // translate b + recipe-by-index into GPU spawn / advance commands.
}
```

**Threading**: NOT thread-safe. One dispatcher per gameplay thread; wrap with an external mutex if multiple systems fire concurrently.

**Dependencies** (CLAUDE.md S7):

| Dep | Visibility | Why |
|---|---|---|
| `cd::core` | PUBLIC | `CD_NODISCARD` + base defines (in header). |
| `cd::math` | PUBLIC | `Vec3f`, `Vec4f`, `Quatf` (in header). |
| `cd::log`  | PRIVATE | `CD_LOG_WARN` on unknown-recipe fires (.cpp only). |

No render / RHI / scene / `cd::render::gpu_particles` includes - the GPU sink lives one tier above and pulls staged data via `active_bursts()`.

**Test command**: `ctest --preset ninja-debug -R cd_test_game_particles_event --output-on-failure`.

**Test coverage** (10 cases):

1. Register + fire spawns burst with N particles.
2. Multiple fires at different positions = independent bursts.
3. Lifetime expires removes burst.
4. Unknown recipe = no-op + log warning.
5. Multiple recipes coexist.
6. Emitter shape kBox vs kSphere yields different bounds.
7. on_emit callback fires once per fire() with correct burst data.
8. unregister_recipe blocks future fires but keeps live bursts alive.
9. alive_count decays monotonically with age.
10. clear_active + reset semantics.

**Notes**:

- `on_emit` callbacks run inline at `fire()` time. They are NOT invoked from `tick()`; "burst died of old age" notifications belong in a consumer that walks `active_bursts()` itself.
- `alive_count` is a render-side hint that decays linearly with age. It is not a particle simulation - the GPU sink owns particle-level lifecycle.
- The dispatcher tombstones recipe storage slots on `unregister_recipe` so live bursts keep their valid recipe index. The slot is reused on the next `register_recipe(same_name, ...)`.

**Design references**:

- Unity Technologies, *VFX Graph - Spawn Events & Spawn Contexts*, Unity Manual (event-driven burst dispatch model).
- Epic Games, *Niagara Effect System - Emitter & System hierarchy*, Unreal Engine Documentation (recipe / instance separation).
- Charles Bloom, *Notes on data-oriented particle systems*, cbloomrants (staged-recipe + render-side spawn pattern).
