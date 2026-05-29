# cd::render::volumetric

## Purpose
CPU baseline for volumetric fog and atmospheric scattering math. Height-fog reference implementation designed to be ported to GPU compute + froxel grid in Phase 8+. Header-only; no external dependencies beyond core and math.

## Namespace
`cd::render::volumetric::` — all public symbols.

## Public headers
- Volumetric fog density and scattering calculations; height-based extinction

## Primary types
- Height-fog density function; mie/rayleigh scattering accumulators

## Usage example
```cpp
#include <cd/render/volumetric/VolmetricFog.hpp>

// Calculate density at a given height
float density = cd::render::volumetric::sample_fog_density(
  /*height=*/position.y,
  /*fog_height=*/100.0f
);

// Accumulate scattering along a ray
auto scattering = cd::render::volumetric::accumulate_scattering(
  /*ray_start=*/cam_pos, /*ray_end=*/hit_pos
);
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_render_volumetric
```

## Test
```bash
ctest --preset ninja-debug -R render_volumetric
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types
- `cd::math` — Vector and ray math

## Notes
- Header-only library
- CPU reference implementation
- GPU froxel-grid port planned for Phase 8
- Part of cd::volumetric_fx umbrella (batch include with volumetric_fog + volumetric_clouds)

## References
- Legacy height-fog reference material
- Phase 7 atmospheric research
