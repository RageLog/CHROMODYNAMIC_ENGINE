# cd::post_fx

## Purpose
Umbrella library aggregating all post-processing effects and light shafts. Single dependency target for samples and applications wanting the complete screen-space and composite effect stack without listing 11 individual dependencies.

## Namespace
`cd::post_fx::` — all public symbols (inherits from member libraries).

## Public headers
- Transitively includes all member post-processing libraries:
  - `cd::post_bloom` — Karis 2013 multi-mip bloom
  - `cd::post_camera` — Vignette, chromatic aberration, film grain
  - `cd::post_composite` — Forward composite shader + Push struct
  - `cd::post_dof` — Sousa 2013 hexagonal bokeh
  - `cd::post_gtao` — Jimenez 2016 horizon scan AO
  - `cd::post_motion_blur` — McGuire 2012 tiled-max
  - `cd::post_smaa` — Jimenez 2012 SMAA T2x antialiasing
  - `cd::post_ssr` — Stachowiak 2015 screen-space reflections
  - `cd::post_taa` — Karis 2014 TAA + Halton jitter
  - `cd::post_tonemap` — ACES, Hill, Hable, AGX tone mappers
  - `cd::light_shafts` — Mitchell 2007 god rays

## Primary types
- Types from each member library (preserved with their namespaces)

## Usage example
```cpp
#include <cd/post_fx/PostFx.hpp>

// All 11 post-FX libraries available in one include
// Samples can use cd::post_fx instead of listing each individually
auto bloom_config = cd::post_bloom::get_config();
auto taa_config = cd::post_taa::get_config();
// ... etc
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_post_fx
```

## Test
```bash
ctest --preset ninja-debug -R post_fx
```

## Dependencies (per CMakeLists)
- `cd::post_bloom`
- `cd::post_camera`
- `cd::post_composite`
- `cd::post_dof`
- `cd::post_gtao`
- `cd::post_motion_blur`
- `cd::post_smaa`
- `cd::post_ssr`
- `cd::post_taa`
- `cd::post_tonemap`
- `cd::light_shafts`

## Notes
- Interface library (aggregation only)
- Phase 234 umbrella
- Each member library keeps its own namespace and documentation
- Samples preferring `cd::post_fx` get the full stack in one include
- Granular dependencies still available for minimal-footprint builds

## References
- Individual library READMEs and papers referenced in member libs
