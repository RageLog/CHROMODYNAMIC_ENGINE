# cd::brdf

## Purpose
Umbrella library aggregating three specialized BRDF lobe implementations (LTC area lights, sheen + clearcoat, subsurface scattering). Consumers can depend on a single target instead of listing all three granular libs.

## Namespace
`cd::brdf::` — all public symbols (inherits from member libraries).

## Public headers
- Transitively includes all three BRDF lobe libraries:
  - `cd::brdf_ltc` — Heitz 2016 analytic area-light integration
  - `cd::brdf_sheen_clearcoat` — Charlie sheen + Filament 2-lobe clearcoat
  - `cd::brdf_sss` — Burley diffusion + Jimenez separable blur

## Primary types
- Types from each member library (preserved with their namespaces)

## Usage example
```cpp
#include <cd/brdf/brdf.hpp>

// All three lobe namespaces available through the one umbrella include.
// LTC area light (Heitz 2016): sample M^-1, integrate a quad analytically.
const auto m = cd::brdf::ltc::ltc_inverse_matrix(/*roughness*/ 0.5F,
                                                 /*n_dot_v*/   0.5F);
const std::array<cd::math::Vec3f, 4> corners { /* tangent-space quad */ };
const float irradiance = cd::brdf::ltc::polygon_irradiance(corners, m);

// Charlie sheen + Filament clearcoat (Estevez & Kulla 2017 / Filament).
const float sheen_d = cd::brdf::sheen_clearcoat::charlie_d(0.3F, /*n_dot_h*/ 0.0F);
const float cc_dv   = cd::brdf::sheen_clearcoat::clearcoat_d_v(0.2F, 1.0F, 1.0F, 1.0F);

// Burley diffusion + Jimenez separable kernel (Burley 2015 / Jimenez 2010).
const auto sss_kernel = cd::brdf::sss::make_burley_kernel(/*taps*/ 8,
                                                          /*scale*/ 1.0F,
                                                          /*radius_mm*/ 5.0F);
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_brdf
```

## Test
```bash
ctest --preset ninja-debug -R brdf
```

## Dependencies (per CMakeLists)
- `cd::brdf_ltc` — Area-light BRDF
- `cd::brdf_sheen_clearcoat` — Sheen and clearcoat lobes
- `cd::brdf_sss` — Subsurface scattering

## Notes
- Interface library (aggregation only)
- Phase 234 umbrella
- Each member library keeps its own namespace and documentation
- Consumers prefer `cd::brdf` over listing three separate deps

## References
- Heitz 2016 — "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines"
- Filament documentation — Multi-lobe material model
- Burley 2015 & Jimenez 2015 — Subsurface scattering techniques
