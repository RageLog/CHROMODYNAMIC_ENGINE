# cd::brdf_sheen_clearcoat

## Purpose
Material layer extensions for PBR: sheen (cloth fibers, velvet) and clear coat (automotive paint, plastic wrapping). Enables multi-layer BRDF composition with separate roughness, tint, and Fresnel per stratum.

## Namespace
`cd::<render>::brdf_sheen_clearcoat::`

## Public headers
- `include/cd/brdf_sheen_clearcoat/Sheen.hpp` — Sheen BRDF layer
- `include/cd/brdf_sheen_clearcoat/ClearCoat.hpp` — Clear coat dielectric layer

## Primary types
- `Sheen::Parameters` — Sheen factor, color, roughness
- `ClearCoat::Parameters` — Clear coat thickness, roughness, IOR

## Usage example
```cpp
#include <cd/brdf_sheen_clearcoat/Sheen.hpp>
#include <cd/brdf_sheen_clearcoat/ClearCoat.hpp>

// Composite sheen + base diffuse.
float sheen_brdf = cd::brdf_sheen_clearcoat::sheen::eval(
  normal, view, light, sheen_params
);

// Composite clear coat + base diffuse.
float clearcoat_brdf = cd::brdf_sheen_clearcoat::clearcoat::eval(
  normal, view, light, clearcoat_params
);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_brdf_sheen_clearcoat
ctest --preset ninja-debug -R brdf_sheen_clearcoat
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Disney BRDF** (Burley 2012, 2015) — base framework
- **Guo et al. 2020**, "Multi-Scale Texture Synthesis using Generative Convolutional Networks" (notes on texture composition)

## Notes
- Header-only library.
- Designed to layer on top of base PBR (metallic/roughness) workflow.
- Sheen is energy-conserving and modulates specular brightness.
- Clear coat adds depth-dependent fresnel and roughness.
