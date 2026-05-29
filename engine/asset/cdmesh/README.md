# cd::asset_cdmesh

## Purpose
Cooked binary mesh format (.cdmesh) for runtime streaming. Producer side: any import path (cd::asset_obj, cd::asset_gltf) → save(). Consumer side: runtime load() directly into a CdMesh struct ready for GPU buffer memcpy.

## Namespace
`cd::asset_cdmesh::` — all public symbols.

## Public headers
- `CdMesh.hpp` — Binary mesh format definition and serialization
- `AssetLoader.hpp` — load() and save() entry points

## Primary types
- `CdMesh` — POD struct: vertex buffer, index buffer, bounds, material ranges
- `VertexAttribute` — Type-safe vertex attribute descriptor (position, normal, texcoord, etc.)
- Mesh serialization functions

## Usage example
```cpp
#include <cd/asset_cdmesh/CdMesh.hpp>

// Produce (from glTF or OBJ importer)
cd::asset_cdmesh::CdMesh mesh = from_gltf(...);
cd::asset_cdmesh::save(mesh, "model.cdmesh");

// Consume (at runtime)
auto mesh = cd::asset_cdmesh::load("model.cdmesh");
gpu_buffer->upload(mesh.vertex_data);
gpu_buffer->upload(mesh.index_data);
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_asset_cdmesh
```

## Test
```bash
ctest --preset ninja-debug -R asset_cdmesh
```

## Dependencies (per CMakeLists)
- `cd::core` — ErrorCode, Result
- `cd::math` — Vector, bounds types

## Format
- Header: magic, version, vertex/index count, bounds AABB
- Per-mesh-segment: material ID, vertex range, index range
- Vertex data (packed layout per CdMesh descriptor)
- Index data (uint16 or uint32, depending on vertex count)

## Notes
- Static library
- Binary format is little-endian, byte-aligned for direct memcpy
- Producer filters choose which vertex attributes to bake
- Consumer reads entire mesh in one syscall; GPU upload is direct

## References
- ADR-006 — Asset pipeline architecture
- Wave 10/12 — Asset loader adapters
