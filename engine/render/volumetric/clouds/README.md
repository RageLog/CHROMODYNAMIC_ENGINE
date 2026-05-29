# cd::volumetric_clouds

## Purpose
Volumetric cloud rendering via ray-marching through noise-based density fields. Schneider 2017 algorithm enables real-time clouds with multiple scattering, temporal coherence, and camera motion compensation. Integrates seamlessly with atmospheric lighting and distant sky.

## Namespace
`cd::<render>::volumetric_clouds::`

## Public headers
- `include/cd/volumetric_clouds/Clouds.hpp` — Cloud density evaluation, ray-march parameters, and multi-scatter kernels

## Primary types
- `VolumetricClouds::CloudParams` — Altitude bounds, density scale, coverage, extinction coefficients
- `VolumetricClouds::RaymarchSettings` — Step count, dithering seed, temporal reprojection weight
- `VolumetricClouds::LightingParams` — Primary/secondary scattering balance, sun/sky contribution

## Usage example
```cpp
#include <cd/volumetric_clouds/Clouds.hpp>

// Configure cloud layer.
cd::volumetric_clouds::CloudParams clouds{
  .altitude_min = 1000.0f,  // meters
  .altitude_max = 2000.0f,
  .density_scale = 0.8f,
  .coverage = 0.6f,
  .type = VolumetricClouds::Type::CUMULUS  // preset parameters
};

cd::volumetric_clouds::RaymarchSettings march{
  .step_count = 32,
  .dither_strength = 1.0f,
  .temporal_reproject = true
};

// Ray-march via compute shader (RHI integration).
// Output: per-pixel cloud color with self-shadowing and scattering.
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_volumetric_clouds
ctest --preset ninja-debug -R volumetric_clouds
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Schneider et al. 2017**, "Real-Time Volumetric Clouds" (I3D)
- **Wentz et al. 2018**, "Improved Cloud Rendering in the Real-Time Terrains Engine" (GDC)

## Notes
- Header-only public API; ray-march implementation in cd::rhi as compute shader.
- Noise patterns typically use Perlin/Simplex noise sampled from tileable volume textures.
- Temporal reprojection smooths jitter from frame-to-frame stochastic sampling.
- Multi-scatter models approximate multiple light bounces within cloud volume.
- Pairs with cd::atmosphere for seamless sky transition and sun lighting.
- Used in Marathon runs for cinematic skybox scenes.
