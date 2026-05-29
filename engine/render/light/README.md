# cd::light

**Purpose**: punctual + area light data model + utility math. Provides the Light struct (point / spot / directional / area), attenuation curves, cascaded-shadow-map split helpers, cluster-grid bookkeeping for forward+ rendering, and physical color-temperature conversion (Kelvin to linear RGB).

**Namespace**: `cd::light`.

**Headers**: `cd/light/{Light,Attenuation,CascadedShadow,ClusterGrid,ColorTemperature}.hpp`.

**Primary types**:
- `cd::light::Light` -- tagged union { type, position, direction, color, intensity, range, inner_cone, outer_cone, area extents }. Plays nicely with cd::ecs as a component.
- `cd::light::Attenuation` -- inverse-square + smooth-cutoff falloff helpers (matches Karis 2013 + Frostbite punctual-light attenuation).
- `cd::light::CascadedShadow` -- splits a directional light frustum into N cascades + builds per-cascade tight light-space orthographic projections.
- `cd::light::ClusterGrid` -- 16x9x24 (default) view-space cluster index, used by the forward+ cluster_pbr / cluster_gpu libraries.
- `cd::light::kelvin_to_linear_rgb(K)` -- physically-based Kelvin-temperature to linear RGB helper.

**Usage**:
```cpp
#include <cd/light/Light.hpp>

cd::light::Light sun {};
sun.type = cd::light::LightType::kDirectional;
sun.direction = { 0.3F, -1.0F, 0.2F };
sun.color = cd::light::kelvin_to_linear_rgb(5600.0F);
sun.intensity = 50.0F;
```

**Test command**: `ctest --preset ninja-debug -R cd_test_light --output-on-failure`.

**Notes**:
- hello_engine sources its 4-light demo + 16-PBR-sphere W8-AR ECS grid through cd::light + cd::ecs (cluster grid + per-light shadow trace via the inline-RT prim pipeline).
- CascadedShadow has 4-cascade default; PSSM split derived from Engel 2007 with min-overlap tweak.
- W8-AJ corner-bias for area-light LTC sampling lives next to LightType::kArea (see ADR-20260528-W8AJ).
