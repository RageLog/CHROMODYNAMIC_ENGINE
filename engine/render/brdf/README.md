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
#include <cd/brdf/Brdf.hpp>

// All three BRDF types available in one include
auto ltc_lobe = cd::brdf_ltc::shade_area_light(...);
auto sheen = cd::brdf_sheen_clearcoat::shade_sheen(...);
auto sss = cd::brdf_sss::shade_subsurface(...);
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
