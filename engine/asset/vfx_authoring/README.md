# cd::asset::vfx_authoring

Asset-tier **VFX authoring helper**. Designers hand-edit
`.vfx.json` files; the engine loads, validates, and hands the
description to `cd::world::particle_system` for runtime spawning.

**Moment**: a VFX artist authors a `jump_dust` effect as JSON, the
hot-reload pipeline picks it up, and the engine spawns the new
particles immediately on the next jump — zero engineer round-trips.

## Public surface

```cpp
namespace cd::asset::vfx_authoring {

struct AuthoredEffect
{
    std::string                id;
    float                      lifetime_seconds;
    uint32_t                   max_particles;
    cd::math::Vec3f            initial_velocity;
    cd::math::Vec3f            gravity;
    float                      size_start, size_end;
    cd::math::Vec3f            colour_start, colour_end;
    float                      alpha_start, alpha_end;
};

cd::expected<void, Error>             save_to_json(const AuthoredEffect&,
                                                   const std::filesystem::path&);
cd::expected<AuthoredEffect, Error>   load_from_json(const std::filesystem::path&);
cd::expected<void, Error>             validate_authored(const AuthoredEffect&);

namespace AuthoringPresets
{
    AuthoredEffect jump_dust();
    AuthoredEffect muzzle_flash();
    AuthoredEffect fire_smoke();
    AuthoredEffect water_splash();
}

}
```

## Presets

The four presets cover the most common gameplay effects and give
designers a known-good starting point:

| Preset           | Lifetime | Max particles | Notes                            |
|------------------|----------|---------------|----------------------------------|
| `jump_dust`      | 0.6 s    |  32           | Beige burst at character feet    |
| `muzzle_flash`   | 0.08 s   |  16           | Bright orange, no gravity        |
| `fire_smoke`     | 2.5 s    | 128           | Black → grey, rising             |
| `water_splash`   | 1.0 s    |  64           | Cyan → translucent, falling      |

## Validation

`validate_authored()` catches:

* `id` is non-empty.
* `lifetime_seconds ∈ [0, 60]`.
* `max_particles ∈ [1, 4096]`.
* Colours each in `[0, 1]^3`.
* Alphas each in `[0, 1]`.
* `size_start, size_end ∈ [0, 10]`.

First-fail by design — the hot-reload UI surfaces one diagnostic at
a time.

## Dependencies

* `cd::core` — `Defines.hpp`, `expected`.
* `cd::asset_json` — `.vfx.json` (de)serialisation.

Does **not** depend on `cd::rhi` or `cd::world::particle_system` —
this library is an authoring + validation layer. Spawning is the
runtime tier's responsibility.
