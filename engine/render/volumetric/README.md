# cd::volumetric

**Purpose**: volumetric effects for dynamic fog, clouds, and atmospheric light-scattering. Provides compute-based density field sampling and volumetric lighting integration.

**Namespace**: `cd::volumetric` (legacy namespace in `include/cd/render/volumetric/`) and `cd::volumetric_*` sub-namespaces.

**Headers**: `cd/volumetric/VolumetricFog.hpp` (Wronski froxel pipeline), `cd/volumetric/fog/Fog.hpp`, `cd/volumetric/clouds/Clouds.hpp`, `cd/render/volumetric/Fog.hpp` (legacy ray-march), and the `cd/volumetric/fx/volumetric_fx.hpp` umbrella include. Header-only.

**Primary types + free functions** (CPU reference + GLSL strings; **implemented + tested**):
- `cd::volumetric::FroxelGrid` / `FroxelGridDesc` / `VolumetricFogSettings` -- the Wronski-2014 160x90x64 froxel pipeline; free functions `inject_cell()`, `integrate_view_ray()`, coord transforms `froxel_to_view()` / `view_to_froxel()`, `slice_to_view_z()` / `slice_thickness()`, `beer_lambert()`. GLSL kernels `kVolFogInjectCS` / `kVolFogIntegrateCS` / `kVolFogCompositeCS`.
- `cd::volumetric::clouds::Settings` + density helpers + `kCloudsMarchCS` GLSL.
- `cd::volumetric::fog::FroxelGrid` / `GridConfig` + `kFogInjectionCS` / `kFogIntegrationCS` GLSL.
- `cd::render::volumetric::FogParams` -- legacy analytic ray-march fog (templated `SigmaFn`).

**Sub-libraries (CMake targets)**: `cd::volumetric` (core) + `cd::volumetric_fog` + `cd::volumetric_clouds` + `cd::volumetric_fx` (umbrella).

**Usage example**:
```cpp
#include <cd/volumetric/VolumetricFog.hpp>

cd::volumetric::FroxelGrid grid;
grid.desc = {};            // 160x90x64 Wronski default
grid.resize();
// inject per-cell scattering, then march front-to-back:
std::vector<cd::math::Vec4f> ray;
cd::volumetric::integrate_view_ray(grid, /*x=*/80, /*y=*/45, ray);
```

**Test command**: `ctest --preset ninja-debug -R "cd_test_volumetric" --output-on-failure`.

**Notes**:
- Phase 420 consolidated volumetric sub-libraries into one umbrella.
- The CPU froxel math + GLSL kernels are byte-equivalent (push-constant-fed); real `.cpp` RHI dispatch is a consumer-integration concern -- see ADR-20260616-band4-render-features-scope §volumetric.
- Compute-heavy; performance depends on grid resolution and temporal reprojection.
