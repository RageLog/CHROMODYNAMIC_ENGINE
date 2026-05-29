# cd::cluster_pbr

## Purpose
GLSL helper library and C++ binding layout for PBR fragment shaders consuming cluster_offsets and light_indices buffers from cd::cluster_gpu. Bridges the gap between GPU cluster culling and screen-space PBR shading.

## Namespace
`cd::cluster_pbr::` — all public symbols.

## Public headers
- `Cluster.glsl` — Exposed as string constant for runtime shader compilation; defines cluster lookup helpers
- Layout documentation of cluster buffer bindings (C++ struct for pipeline wiring)

## Primary types
- Binding layout struct documenting SSBO indices for cluster_offsets and light_indices
- GLSL helper functions (exported as string) for cluster-to-light-list lookup

## Usage example
```cpp
#include <cd/cluster_pbr/ClusterHelpers.hpp>

// In a PBR fragment shader (GLSL string):
// #include "cluster_helpers.glsl"
// lights_for_pixel = lights_in_cluster(cluster_coord);

// C++ side: wire up SSBOs to match cd::cluster_pbr layout
cd::cluster_pbr::BindingLayout layout = {...};
// Apply layout to pipeline descriptors
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_cluster_pbr
```

## Test
```bash
ctest --preset ninja-debug -R cluster_pbr
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types

## Notes
- Interface library; no compiled code
- GLSL source embedded as string constant for runtime compilation
- Part of cd::cluster_fx umbrella (batch include cluster_gpu + cluster_pbr + cluster)

## References
- ADR-083 — Forward+ light culling architecture
- Wave 104 — GPU cluster compute
- Phase 9 Sprint 2 — Closes routing item #1 (Forward+ PBR integration)
