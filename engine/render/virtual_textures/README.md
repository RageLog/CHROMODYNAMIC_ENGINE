# cd::virtual_textures

## Purpose
Virtual texture space management and sparse binding primitives. Enables on-demand streaming of high-resolution textures with sub-page residency tracking and automatic feedback-driven request generation.

## Namespace
`cd::virtual_textures::` — all public symbols.

## Public headers
- Virtual texture page allocator, page request system, residency utilities

## Primary types
- Virtual texture space descriptor; page request queue; page feedback reader

## Usage example
```cpp
#include <cd/virtual_textures/VirtualTexture.hpp>

// Create virtual texture space (page-aligned)
auto vt_space = cd::virtual_textures::create_space(/*tile_size=*/128);

// Submit page requests (typically from a feedback buffer)
vt_space->request_pages(feedback_buffer);

// Bind resident pages to physical texture
vt_space->update_residency_map();
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_virtual_textures
```

## Test
```bash
ctest --preset ninja-debug -R virtual_textures
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types
- `cd::math` — Math primitives

## Notes
- Header-only library
- Pairs with cd::virtual_geometry for complete sparse memory system
- Feedback-based resident set management

## References
- GPU virtual memory research (Kaplanyan et al.)
