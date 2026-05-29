# cd::virtual_geometry

## Purpose
Virtual geometry system primitives — sparse texturing, streaming LOD meshes, and on-demand residency tracking. Foundation for scale-independent (Nanite-style) geometry rendering with sub-page memory footprint.

## Namespace
`cd::virtual_geometry::` — all public symbols.

## Public headers
- Core virtual geometry types and residency tracking utilities

## Primary types
- Virtual page allocator; residency map; sparse texture descriptors

## Usage example
```cpp
#include <cd/virtual_geometry/VirtualGeometry.hpp>

// Create virtual memory space for geometry
auto virt_space = cd::virtual_geometry::create_virtual_space(/*page_size=*/4096);

// Request resident pages on demand
virt_space->request_pages({page_ids...});

// Stream LOD meshes via on-demand page faults
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_virtual_geometry
```

## Test
```bash
ctest --preset ninja-debug -R virtual_geometry
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types
- `cd::math` — Math primitives

## Notes
- Header-only library
- Designed for Phase 8+ (GPU-resident streaming); CPU baseline in Phase 7
- Pairs with cd::virtual_textures for texture-side sparse memory

## References
- Unreal Nanite architecture reference
- GPU-resident geometry rendering techniques
