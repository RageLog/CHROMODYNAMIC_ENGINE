# cd::volumetric_fog

## Purpose
Froxel-grid volumetric fog using Wronski 2014 algorithm. Performs per-froxel light injection and then slices the grid for screen-space ray-marching, enabling dynamic scattering, temporal reprojection, and seamless integration with HDR rendering.

## Namespace
`cd::<render>::volumetric_fog::`

## Public headers
- `include/cd/volumetric_fog/Froxel.hpp` — Grid voxel layout and projection math
- `include/cd/volumetric_fog/InjectionPass.hpp` — Light contribution compute shader descriptor
- `include/cd/volumetric_fog/RaymarchPass.hpp` — Screen-space march and reprojection parameters

## Primary types
- `VolmetricFog::FroxelGrid` — 3D (XY × depth) grid dimensions and world-space bounds
- `VolmetricFog::InjectionPass` — Light source enumeration for froxel illumination
- `VolmetricFog::RaymarchPass` — March step count, density modulation, history blend factor

## Usage example
```cpp
#include <cd/volumetric_fog/Froxel.hpp>

// Initialize froxel grid (e.g., 160×90×32 for 1080p).
cd::volumetric_fog::FroxelGrid grid{
  .resolution = glm::uvec3(160, 90, 32),
  .near_plane = 0.1f,
  .far_plane = 500.0f,
  .density_modulation = 1.5f
};

// Injection + raymarch via RHI compute passes.
// Results composite into main color target.
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_volumetric_fog
ctest --preset ninja-debug -R volumetric_fog
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Wronski 2014**, "Volumetric Fog: Unified Compute Shader Approach" (SIGGRAPH)
- Integration: cd::rhi framegraph patterns

## Notes
- Header-only public API; GPU compute in cd::rhi.
- Froxel reprojection handles camera movement without per-frame recomputation.
- Density and scattering controlled via per-frame push constants.
- Pairs with cd::volumetric_clouds for layered atmospheric effects.
