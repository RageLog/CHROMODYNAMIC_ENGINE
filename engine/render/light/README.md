# cd::light

**Purpose**: punctual + area light data model + utility math. Provides the Light struct (point / spot / directional / area), attenuation curves, cascaded-shadow-map split helpers, cluster-grid bookkeeping for forward+ rendering, and physical color-temperature conversion (Kelvin to linear RGB).

**Namespace**: `cd::light`.

**Headers**: `cd/light/{Light,Attenuation,CascadedShadow,ClusterGrid,ColorTemperature}.hpp`.

**Primary types + free functions**:
- `cd::light::Light` -- 112-byte std140-packable POD { type, position, direction, color, intensity, range, cone terms, area extents, slots }. Plays nicely with cd::ecs as a component. Construct via `directional()` / `point()` / `spot()` / `rect_area()`.
- `Attenuation.hpp` -- free functions `distance_attenuation()` (Frostbite windowed inverse-square) + `cone_attenuation()` + `lumens_to_point_intensity()` / `lumens_to_spot_intensity()` / `lux_to_directional_intensity()`.
- `CascadedShadow.hpp` -- free functions `practical_split_distances()` + `slice_frustum_corners_world()` + `fit_cascade_light_matrix()` + `build_cascades()` (splits a directional light frustum into N cascades + builds per-cascade tight light-space ortho projections).
- `cd::light::ClusterGrid` -- default 16x9x24 view-space cluster index, **data-only froxel copy**. SEALED data-only-v1; it migrates to the single froxel owner `cd::lighting_clusters::Clusterer` (see ADR-20260616-band4-render-features-scope §light + ADR-20260616-band3-render-features-scope §5).
- `cd::light::cct_to_linear_rgb(kelvin)` -- correlated-colour-temperature (Kelvin) to linear sRGB (Krystek 1985 / CIE-1931 fit).

**Usage**:
```cpp
#include <cd/light/Light.hpp>
#include <cd/light/ColorTemperature.hpp>

auto sun = cd::light::directional({ 0.3F, -1.0F, 0.2F },
                                  cd::light::cct_to_linear_rgb(5600.0F),
                                  /*lux=*/50000.0F);
```

**Test command**: `ctest --preset ninja-debug -R cd_test_light --output-on-failure`.

**Notes**:
- hello_engine sources its 4-light demo + 16-PBR-sphere W8-AR ECS grid through cd::light + cd::ecs (cluster grid + per-light shadow trace via the inline-RT prim pipeline).
- CascadedShadow has 4-cascade default; PSSM split derived from Engel 2007 with min-overlap tweak.
- W8-AJ corner-bias for area-light LTC sampling lives next to LightType::kArea (see ADR-20260528-W8AJ).
