# cd::render

## Purpose
High-level renderer orchestrating per-frame command buffers, synchronization primitives, swapchain acquire/present, and implicit image-layout transitions. Sits on top of cd::rhi abstraction; cd::material is a peer (renderer doesn't prescribe material representation).

## Namespace
`cd::render::` — all public symbols.

## Public headers
- `Renderer.hpp` — Main Renderer interface and frame submission

## Primary types
- `Renderer` — Per-frame state machine; manages swapchain sync, frame boundaries, command recording
- Frame submission and present callbacks

## Usage example
```cpp
#include <cd/render/Renderer.hpp>

// Create renderer on top of initialized RHI device
auto renderer = cd::render::create_renderer(device, allocator);

// Per frame
renderer->begin_frame();
// ... record commands via framegraph ...
renderer->end_frame();
renderer->present();
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_render
```

## Test
```bash
ctest --preset ninja-debug -R render
```

## Dependencies (per CMakeLists)
- `cd::rhi` — Vulkan/D3D12/OpenGL abstraction (Device, CommandBuffer)
- `cd::core` — Core types

## Notes
- Static library
- Backend-agnostic (consumes only cd::rhi interface)
- Pairs with cd::framegraph for render pass management and cd::material for shading
- Sprint S3.9 — finalized high-level renderer API

## References
- ADR-002 — Renderer architecture
- RHI abstraction layer documentation
