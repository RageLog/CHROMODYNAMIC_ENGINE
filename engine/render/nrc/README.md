# cd::nrc

## Purpose
Neural Radiance Cache (NRC) — learned implicit representation of ray-traced radiance fields using small neural networks. Accelerates monte-carlo path tracing via task-parallelizable inference, achieving interactive GI quality with orders of magnitude fewer samples than brute-force ray tracing.

## Namespace
`cd::<render>::nrc::`

## Public headers
- `include/cd/nrc/Nrc.hpp` — Network evaluation kernel, cache coherency helpers, input preparation

## Primary types
- `Nrc::Network` — Multi-layer perceptron descriptor (layer dims, activation fn, weight tensor)
- `Nrc::RadianceQuery` — Input vector (position, direction, normal) + output confidence

## Usage example
```cpp
#include <cd/nrc/Nrc.hpp>

// Query learned radiance field.
cd::nrc::RadianceQuery query{
  .position = world_pos,
  .direction = normalized_ray_dir,
  .normal = surface_normal
};

glm::vec3 radiance = cd::nrc::Nrc::evaluate(network, query);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_nrc
ctest --preset ninja-debug -R nrc
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Müller et al. 2021**, "Neural Radiance Caching for Path Tracing" (TOG/SIGGRAPH)
- Research: `research/library/MANIFEST.csv` for linked PDF + methodology

## Notes
- Header-only library (CPU-side network inference; GPU compute implementation in cd::rhi).
- Pre-training required on target scene via offline path tracing → supervised learning phase.
- Designed for hybrid rendering: combines fast NRC inference with occasional fine-quality sample refinement.
- Particularly effective for glossy-to-diffuse indirect illumination.
