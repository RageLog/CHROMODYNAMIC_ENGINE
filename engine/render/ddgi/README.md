# cd::ddgi

## Purpose
Dynamic Diffuse Global Illumination via probe-based irradiance caching. Maintains a sparse grid of probes that trace rays in compute to update diffuse radiance, enabling real-time GI for moving geometry and lighting changes without full path tracing cost.

## Namespace
`cd::<render>::ddgi::`

## Public headers
- `include/cd/ddgi/Ddgi.hpp` — Probe grid setup, ray dispatch, irradiance classification

## Primary types
- `Ddgi::ProbeGrid` — Spatial hash of probe positions and ray history
- `Ddgi::RayDispatch` — Per-frame ray trace work item bundle
- `Ddgi::IrradianceClassifier` — Brightness heuristic for adaptive probe relocation

## Usage example
```cpp
#include <cd/ddgi/Ddgi.hpp>

// Create a 4×4×4 probe grid over the scene.
cd::ddgi::ProbeGrid grid{
  .grid_size = glm::ivec3(4, 4, 4),
  .world_origin = glm::vec3(0.0f),
  .probe_spacing = glm::vec3(2.0f)
};

// Dispatch rays from all probes.
auto dispatch = grid.make_ray_dispatch(/*frame_index*/ 0);

// Apply results in deferred shading to query diffuse contribution.
glm::vec3 diffuse = grid.sample_irradiance(world_pos, normal);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_ddgi
ctest --preset ninja-debug -R ddgi
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Majercik et al. 2019**, "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Probes" (I3D)
- Architecture notes: `docs/ADR/ADR-*-ddgi-*.md`

## Notes
- Header-only INTERFACE library.
- Probe rays are typically traced on GPU via compute shader (cd::rhi integration).
- Supports probe relocation, temporal filtering, and multi-bounce approximation.
- Complements ray tracing and baked GI for dynamic scenarios (character lighting, moving objects).
