# cd::cluster

**Purpose**: clustered light aggregation for efficient many-light shading. Groups lights into screen-space clusters and provides GPU-resident data structures for per-cluster light lists.

**Namespace**: `cd::cluster`.

**Headers**: `cd/render/cluster/*.hpp` (legacy namespace) and `cd/cluster/{gpu,pbr,fx}/*.hpp`.

**Primary types**:
- `cd::cluster::ClusterGrid` -- screen-space cluster discretization (min/max depth per tile).
- GPU/compute pipeline types in `cd::cluster_gpu` for cluster list building.
- PBR light-assignment helpers in `cd::cluster_pbr`.
- Post-effects variants in `cd::cluster_fx`.

**Sub-libraries**:
- `cd::cluster_gpu` -- compute shaders for cluster light-list building.
- `cd::cluster_pbr` -- PBR-specific cluster assignments.
- `cd::cluster_fx` -- effects-targeted variants.

**Usage example**:
```cpp
#include <cd/cluster/ClusterGrid.hpp>
// Use cluster data for light-culling compute passes
```

**Test command**: `ctest --preset ninja-debug -R cd_test_cluster --output-on-failure`.

**Notes**:
- Phase 420 consolidated multiple cluster sub-libraries into one umbrella.
- Shader sources in `shaders/` directory.

**TODO**: expand coverage (currently <3 test cases).
