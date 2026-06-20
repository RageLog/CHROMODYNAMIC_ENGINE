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
- The froxel-assignment binning here intentionally mirrors `cd::lighting_clusters`
  and `cd::light::ClusterGrid` (documented duplication, not a refactor target) —
  see `docs/PROJECT_COMPLETION_STATUS.md` §5.

**Test coverage**:
- `cd_test_cluster` — host CPU-reference path: froxel grid dims, finalize/clear
  lifecycle, sphere in/out/spanning the frustum, near/far depth slices,
  count-then-write two-pass invariants, offset monotonicity, GridReference
  parity.
- `cd_test_cluster_pbr` — Forward+ lookup contract: push-constant field offsets,
  descriptor-binding indices, GLSL helper structure (SSBO decls, set-override
  macro, log-Z + angle helpers, light-struct wire parity), real glslang compile.
- `cd_test_cluster_gpu` — Vulkan-gated two-pass count/write pipeline parity vs.
  `run_reference_compute` (single + many lights, empty list) plus negative
  paths (zero max_lights, over-budget run). GTEST_SKIPs without a Vulkan ICD.
- `cd_test_cluster_fx` — umbrella one-symbol-per-sub-lib link smoke.
