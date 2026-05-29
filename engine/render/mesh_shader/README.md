# cd::mesh_shader

## Purpose
Meshlet-based geometry processing primitives for VK_EXT_mesh_shader hardware. Clusters ~64 vertices / ~124 triangles per meshlet with pre-computed bounding sphere and cone for culling (backface, frustum, occlusion) on the task shader.

## Namespace
`cd::mesh_shader::` — all public symbols.

## Public headers
- `Meshlet.hpp` — Core meshlet struct and cluster builder utilities

## Primary types
- `Meshlet` — 64-vertex / 124-triangle cluster with bounding sphere, normal cone, and index range
- Bounding sphere for occlusion culling; cone for backface rejection; AABB for frustum culling

## Usage example
```cpp
#include <cd/mesh_shader/Meshlet.hpp>

// Build meshlets from raw geometry
std::vector<cd::mesh_shader::Meshlet> meshlets = 
  cd::mesh_shader::cluster_geometry(vertex_buffer, index_buffer);

// On GPU (mesh_shader / task_shader):
// Per-meshlet culling: frustum, backface, occlusion
// Mesh shader streams vertices/indices from storage SSBOs
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_mesh_shader
```

## Test
```bash
ctest --preset ninja-debug -R mesh_shader
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types
- `cd::math` — Vector, sphere, cone math

## Notes
- Header-only library
- Leaf-level granularity; meshlets are the base unit for Nanite-style hierarchical culling
- Consumes storage SSBOs for vertex/index buffers (no traditional VAO)
- Designed for Wave 100+ (future Nanite-style implementation)

## References
- Akenine-Möller et al. 2018 ch. 10.5 — meshlet background
- NVIDIA "Introduction to Mesh Shaders" 2018
- Karis 2021 — Nanite reference architecture
