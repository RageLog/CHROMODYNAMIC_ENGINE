# cd::ui_renderer_rhi

**Purpose**: Bridges `cd::ui::renderer::DrawBatcher` (pure CPU) to `cd::rhi::IDevice`. Owns ring vertex/index buffers + an atlas-binding descriptor; uploads per frame; records draws into the caller's command buffer. Phase 1.2b of ADR-20260530-ui-widget-library.

**Namespace**: `cd::ui::renderer_rhi`.

**Headers**: `cd/ui/renderer_rhi/Submitter.hpp`.

**Primary type**:
- `cd::ui::renderer_rhi::Submitter` — move-only owning handle.
  - `create(device, info)` -> `Result<Submitter>` — allocates vertex + index buffers + atlas descriptor.
  - `upload(batcher)` — copies the batcher's vertex/index spans into the GPU ring buffers; returns false on overflow.
  - `record(cmd, viewport_extent)` — iterates the batcher's `DrawCommand` list and issues one `set_scissor + draw_indexed` per group.
  - `destroy()` — idempotent; called automatically on destruction.

**Lifecycle**:
```cpp
#include <cd/ui/renderer_rhi/Submitter.hpp>

cd::ui::renderer_rhi::SubmitterCreateInfo info {};
info.color_format = cd::rhi::Format::kRGBA8Unorm;
info.max_vertices = 65535U;
info.max_indices  = 65535U * 6U;
info.atlas_view    = my_font_atlas_view;
info.atlas_sampler = my_linear_sampler;

auto sub = cd::ui::renderer_rhi::Submitter::create(*device, info);
if (!sub) return /* error */;

// per frame:
batcher.begin_frame();
// ... batcher.quad / textured_quad / glyph ...
if (!sub->upload(batcher)) { /* overflow, split or grow */ }

cmd->begin_render_pass(/*...*/);
sub->record(*cmd, { width, height });
cmd->end_render_pass();
```

**Phase 1.2b scope** (this library):
- vb + ib ring allocation
- upload via `IDevice::upload_buffer` (kCpuToGpu memory)
- `DrawCommand` iteration with scissor + bound vb/ib + draw_indexed
- NullDevice-validated lifecycle (create / destroy / upload-overflow guard)

**Out of Phase 1.2b** (Phase 1.5 hello_ui sample owns these):
- UI pipeline (VS + FS) creation via cd::material
- Per-variant pipeline binding (kSolid / kTextured / kGlyph)
- Atlas descriptor set updates beyond create-time bind
- Push-constant projection matrix upload

**Test command**: `ctest --preset ninja-debug -R cd_test_submitter --output-on-failure`. Covers ~6 cases: create/zero-capacity/destroy-idempotent/upload-success/upload-overflow/record-smoke.

**Notes**:
- Real GPU validation belongs in the Phase 1.5 `hello_ui` sample with a Vulkan device. This library proves the API shape + lifecycle is sound; the draw-call recording works against NullDevice without crashing.
- Multiple atlas textures = multiple Submitters (the design accepts one atlas per submitter for cache-locality + descriptor-set simplicity). A Phase 2 variant with bindless atlas array can follow without changing the API.
- 16-bit index (max 65535 vertices) matches DrawBatcher's index format. Auto-split on overflow is Phase 2.
