# cd::volumetric

**Purpose**: volumetric effects for dynamic fog, clouds, and atmospheric light-scattering. Provides compute-based density field sampling and volumetric lighting integration.

**Namespace**: `cd::volumetric` (legacy namespace in `include/cd/render/volumetric/`) and `cd::volumetric_*` sub-namespaces.

**Headers**: `cd/render/volumetric/*.hpp` (core) and `cd/volumetric/{fog,clouds,fx}/*.hpp` (sub-effects).

**Primary types**:
- `cd::volumetric::VolumetricGrid` -- 3D density-field discretization.
- `cd::volumetric_fog::FogPass` -- volumetric fog renderer.
- `cd::volumetric_clouds::CloudPass` -- procedural cloud rendering.
- `cd::volumetric_fx::VolumetricEffects` -- general volumetric integration helpers.

**Sub-libraries**:
- `cd::volumetric_fog` -- Fog effect implementation.
- `cd::volumetric_clouds` -- Cloud effect implementation.
- `cd::volumetric_fx` -- Effect-variant wrappers.

**Usage example**:
```cpp
#include <cd/volumetric/fog/FogPass.hpp>
// Apply volumetric fog to scene
```

**Test command**: `ctest --preset ninja-debug -R "cd_test_volumetric_.*" --output-on-failure`.

**Notes**:
- Phase 420 consolidated volumetric sub-libraries into one umbrella.
- Shader sources in respective `shaders/` subdirectories.
- Compute-heavy; performance depends on grid resolution and temporal reprojection.

**TODO**: expand coverage (currently <3 test cases).
