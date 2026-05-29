# cd::render::cluster

## Purpose
CPU baseline reference implementation for clustered forward+ light culling. Builds a 3D X × Y × log(Z) grid over the view frustum and assigns lights to clusters based on their sphere-AABB overlap. Designed to be ported directly to GPU compute in later phases.

## Namespace
`cd::render::cluster::` — all public symbols.

## Public headers
- `ClusterGrid.hpp` — Main cluster grid builder; stores offsets and per-cluster light indices
- `ReferenceCompute.hpp` — Reference C++ compute kernel for cluster assignment

## Primary types
- `ClusterGrid` — 3D cluster grid with light assignment tracking; build is two-pass (assign_light → finalize)
- Cluster dimensions: X × Y × log(Z) over view-space frustum; Z-spacing logarithmic for depth precision

## Usage example
```cpp
#include <cd/render/cluster/ClusterGrid.hpp>

// Create grid sized 16×16 with Z-slices matching camera near/far
cd::render::cluster::ClusterGrid grid{16, 16, /*z_slices=*/8};

// Assign lights (view-space sphere {center, radius})
grid.assign_light(/*cluster_id=*/0, /*light_index=*/0);
// ...more assignments...

// Pack into offsets/indices arrays
grid.finalize();

// Fetch light list for a cluster
auto lights = grid.lights_in_cluster(cluster_id);
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_render_cluster
```

## Test
```bash
ctest --preset ninja-debug -R render_cluster
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types and utilities
- `cd::math` — Vector and math primitives

## Notes
- Header-only library; includes carry no binary footprint
- Reference implementation; GPU compute port (cluster_gpu) lands in Phase 9
- Designed for direct translation to VK_EXT_cluster_compute (future)

## References
- ADR-083 — Wave 83 Forward+ architecture decision
- Wave 84-85 — CPU baseline development
