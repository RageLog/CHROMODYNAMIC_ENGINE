# cd::volumetric_fx

## Purpose
Umbrella library aggregating three volumetric effect libraries: legacy height-fog reference math, Wronski froxel grid, and Schneider cloud ray-marching. Consumers depend on a single target for complete volumetric atmosphere stack.

## Namespace
`cd::volumetric_fx::` — all public symbols (inherits from member libraries).

## Public headers
- Transitively includes all three volumetric libraries:
  - `cd::render::volumetric` — CPU baseline height-fog math
  - `cd::volumetric_fog` — Wronski 2014 froxel grid implementation
  - `cd::volumetric_clouds` — Schneider 2017 cloud ray-march

## Primary types
- Types from each member library (preserved with their namespaces)

## Usage example
```cpp
#include <cd/volumetric_fx/VolumetricFx.hpp>

// All three volumetric systems available
auto fog_density = cd::render::volumetric::sample_fog_density(...);
auto froxel_lighting = cd::volumetric_fog::sample_froxel(...);
auto cloud_color = cd::volumetric_clouds::raymarch_clouds(...);
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_volumetric_fx
```

## Test
```bash
ctest --preset ninja-debug -R volumetric_fx
```

## Dependencies (per CMakeLists)
- `cd::volumetric` — CPU height-fog baseline
- `cd::volumetric_fog` — Froxel grid
- `cd::volumetric_clouds` — Cloud ray-march

## Notes
- Interface library (aggregation only)
- Phase 234 umbrella
- Each member library keeps its own namespace and documentation
- Comprehensive volumetric atmosphere solution in single dep

## References
- Wronski 2014 — Froxel-based volumetric lighting
- Schneider 2017 — Realistic cloud ray-marching techniques
