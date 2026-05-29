# cd::restir_di

## Purpose
ReSTIR direct illumination — weighted reservoir sampling (WRS) for per-pixel light source selection with temporal and spatial reuse. Reduces variance in multi-light stochastic shadow sampling by orders of magnitude with minimal overhead.

## Namespace
`cd::<render>::restir_di::`

## Public headers
- `include/cd/restir_di/Reservoir.hpp` — WRS sample container and combine semantics
- `include/cd/restir_di/ReuseStrategy.hpp` — Temporal/spatial reuse patterns

## Primary types
- `Restir_di::Reservoir` — Selected light index + sample weight + validity flags
- `Restir_di::ReusePattern` — Neighbor offset sets for spatial taps

## Usage example
```cpp
#include <cd/restir_di/Reservoir.hpp>

// Initialize per-pixel reservoir.
cd::restir_di::Reservoir reservoir;

// Stream multiple random light samples, update WRS.
for (int i = 0; i < num_candidate_lights; ++i) {
  float weight = evaluate_light(lights[i], current_pixel);
  reservoir.update(lights[i].id, weight, rng());
}

// Retrieve final selected light.
int selected_light = reservoir.selected_idx();
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_restir_di
ctest --preset ninja-debug -R restir_di
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Bitterli et al. 2020**, "Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting" (SIGGRAPH)
- ADR: `docs/ADR/ADR-*-restir-di-*.md`

## Notes
- Header-only CPU reference + GLSL compute shader helpers.
- Dramatically improves convergence in denoised ray tracing (denoise layer absorbs temporal jitter).
- Supports temporal reuse (history from prior frame) and spatial reuse (neighbor consensus).
- Integrates with multi-bounce path tracing via cd::restir_gi.
