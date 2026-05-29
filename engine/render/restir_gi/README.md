# cd::restir_gi

## Purpose
ReSTIR global illumination — extends weighted reservoir sampling to bounce-sample selection for arbitrary-bounce path tracing. Enables efficient importance sampling of bounce directions with temporal/spatial reuse, achieving convergence on challenging indirect lighting scenarios.

## Namespace
`cd::<render>::restir_gi::`

## Public headers
- `include/cd/restir_gi/BounceSample.hpp` — Bounce direction reservoir and history
- `include/cd/restir_gi/ReuseKernel.hpp` — Spatial neighbor tap patterns

## Primary types
- `Restir_gi::BounceSample` — Sampled bounce direction + PDF + visibility history
- `Restir_gi::ReuseKernel` — Correlation window for spatial reuse compatibility

## Usage example
```cpp
#include <cd/restir_gi/BounceSample.hpp>

// Multi-bounce path tracing with ReSTIR.
cd::restir_gi::BounceSample bounce;

for (int bounce_idx = 0; bounce_idx < max_bounces; ++bounce_idx) {
  // Sample next direction via WRS from candidate directions.
  bounce = reservoir_sample_direction(
    current_ray, radiance_candidates, rng()
  );
  
  // Trace next segment.
  Ray next_ray = current_ray.bounce_along(bounce.direction);
  radiance += trace(next_ray, bounce.pdf);
}
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_restir_gi
ctest --preset ninja-debug -R restir_gi
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Ouyang et al. 2021**, "ReSTIR GI: Path Resampling for Real-Time Global Illumination" (SIGGRAPH)
- **Dahlberg et al. 2022**, "Improved Resampling for Global Illumination" (survey)

## Notes
- Header-only CPU reference + GLSL kernel support.
- Complements direct illumination ReSTIR (cd::restir_di) for full-path convergence.
- Requires temporal coherency for maximum efficiency.
- Pairs with denoisers (cd::denoise) for production quality.
