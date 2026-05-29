# cd::cluster_fx

## Purpose
Umbrella library aggregating the three cluster-related libraries: CPU baseline grid, GPU compute pipeline, and PBR binding helpers. Single dependency target for forward+ light culling in both CPU reference and GPU production modes.

## Namespace
`cd::cluster_fx::` — all public symbols (inherits from member libraries).

## Public headers
- Transitively includes all three cluster libraries:
  - `cd::cluster` — CPU cluster grid build + reference compute
  - `cd::cluster_gpu` — Vulkan compute pipeline wiring
  - `cd::cluster_pbr` — Per-pixel cluster lookup helper for PBR FS

## Primary types
- Types from each member library (preserved with their namespaces)

## Usage example
```cpp
#include <cd/cluster_fx/ClusterFx.hpp>

// Reference CPU path
cd::cluster::ClusterGrid grid{16, 16, 8};

// Or production GPU path
auto gpu_pipeline = cd::cluster_gpu::create_gpu_pipeline(...);

// PBR integration helpers available
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_cluster_fx
```

## Test
```bash
ctest --preset ninja-debug -R cluster_fx
```

## Dependencies (per CMakeLists)
- `cd::cluster` — CPU grid and reference compute
- `cd::cluster_gpu` — Vulkan compute pipeline
- `cd::cluster_pbr` — PBR shading integration

## Notes
- Interface library (aggregation only)
- Excludes from install (transitive on vendored glslang via cluster_gpu)
- Phase 234 umbrella
- Each member library keeps its own namespace and documentation
- Complete forward+ solution in one dep (Wave 83-104 closure)

## References
- ADR-083 — Forward+ architecture decision
- Wave 83-104 — Progressive CPU → GPU cluster implementation
