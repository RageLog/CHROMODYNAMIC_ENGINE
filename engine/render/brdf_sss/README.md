# cd::brdf_sss

## Purpose
Subsurface scattering (SSS) shading with Burley 2015 diffuse profile and Jimenez 2010 separable blur kernels. Enables believable skin, wax, marble, and translucent materials via screen-space or world-space integration.

## Namespace
`cd::<render>::brdf_sss::`

## Public headers
- `include/cd/brdf_sss/Sss.hpp` — SSS profile evaluation and blur kernel helpers

## Primary types
- `Sss::Profile` — Per-material SSS parameters (scatter distance, falloff, mean free path)
- `Sss::BlurKernel` — Precomputed 1D kernel samples for separable approximation

## Usage example
```cpp
#include <cd/brdf_sss/Sss.hpp>

// Evaluate separable SSS blur with Burley profile.
cd::brdf_sss::Profile sss_profile{
  .scatter_distance = 4.0f,  // mm or scene units
  .falloff = glm::vec3(1.0f, 0.5f, 0.3f)  // RGB channel falloff
};

float sss_contrib = cd::brdf_sss::evaluate(
  uv, sss_profile, thickness_map
);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_brdf_sss
ctest --preset ninja-debug -R brdf_sss
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Burley 2015**, "Extending the Disney BRDF to a Practice-Friendly Shading Model" (SIGGRAPH)
- **Jimenez et al. 2010**, "Real-Time Skin Rendering with Subsurface Scattering" (SIGGRAPH Asia)

## Notes
- Header-only library.
- Typically used in screen-space post-process phase or deferred forward pass.
- Pairs well with thickness maps and normal maps for high-quality skin/foliage rendering.
