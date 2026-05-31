# cd::render

**Purpose**: high-level render orchestration layer. Manages per-frame command buffer recording, synchronization primitives, swapchain acquire/present, and implicit resource-state transitions. Sits directly atop `cd::rhi`.

**Namespace**: `cd::render`.

**Headers**: `cd/render/{ClearColorPreset,DrawBatchKey,DrawBucket,MeshStats,MeshUpload}.hpp` and core render command interface.

**Primary types**:
- `cd::render::Renderer` -- top-level render orchestrator (acquires swapchain images, records per-pass command buffers, handles implicit layout transitions, presents).
- Helper types for batch rendering: `ClearColorPreset`, `DrawBatchKey`, `DrawBucket` for organizing draw calls by state.
- `MeshStats` / `MeshUpload` -- diagnostics and mesh data submission.

**Sub-libraries**: This is an umbrella library. Key sub-components are:
- `cd::rhi` -- RHI abstraction (in `render/rhi`).
- `cd::shader` -- shader compilation and reflection (in `render/shader`).
- `cd::material` -- material system (in `render/material`).
- `cd::light` / `cd::camera` -- light and camera primitives.
- `cd::framegraph` -- frame graph executor.
- `cd::post_*` libraries -- post-processing effects (bloom, TAA, tone-mapping, etc.).
- `cd::cluster` -- clustered light aggregation.
- `cd::volumetric_*` -- volumetric fog and cloud effects.

**Usage example**:
```cpp
#include <cd/render/Renderer.hpp>
// Renderer created and managed by application layer
```

**Test command**: `ctest --preset ninja-debug -R cd_test_render --output-on-failure`.

**Notes**:
- Umbrella library; most rendering functionality lives in sub-libraries.
- See `ADR-002-renderer-architecture.md` for the layered design rationale.

**TODO**: expand coverage (currently <3 test cases).
