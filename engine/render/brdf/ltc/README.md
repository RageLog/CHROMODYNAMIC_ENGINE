# cd::brdf_ltc

## Purpose
Linearly Transformed Cosines (LTC) analytical solution for area light shading. Heitz 2016 pre-computed lookup tables enable exact rectangular/polygonal light BRDF integration without Monte Carlo sampling, yielding gallery-quality specular highlights on arbitrary geometry in a single pixel shader.

## Namespace
`cd::<render>::brdf_ltc::`

## Public headers
- `include/cd/brdf_ltc/Ltc.hpp` — LTC BRDF data structures and evaluation kernels

## Primary types
- `Ltc::Matrix` — 3x3 transform from incoming direction to LTC space
- `Ltc::Evaluation` — Per-pixel LTC integration result (specular radiance + visibility factor)

## Usage example
```cpp
#include <cd/brdf_ltc/Ltc.hpp>

// Approximate specular BRDF on rectangular light via pre-computed LTC.
// Inputs: viewing angle, roughness, light corners (view-space).
// Output: integrated specular contribution.
float ltc_specular = cd::brdf_ltc::evaluate(
  normal, view_dir, roughness,
  light_corner_1, light_corner_2,
  light_corner_3, light_corner_4
);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_brdf_ltc
ctest --preset ninja-debug -R brdf_ltc
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Heitz et al. 2016**, "Real-Time Area Lighting with Linearly Transformed Cosines" (SIGGRAPH)
- ADR path (if applicable): `docs/ADR/ADR-*-ltc-*.md`

## Notes
- Header-only INTERFACE library.
- LTC matrix data is typically loaded from offline-precomputed tables (e.g., from Heitz et al. supplementary materials).
- Integrates seamlessly with PBR pipeline as a specular component alongside Fresnel & roughness filtering.
