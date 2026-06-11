# cd::debug_draw — Debug Line Renderer (GPU)

The GPU half of the debug-line stack. `cd::debug_line` batches shapes
on the CPU; this library owns everything needed to put them on
screen: the `kLineList` pipeline, a lazily-grown host-visible vertex
buffer with a frames-in-flight-safe growth path, and the
once-per-pass flush.

## Why it exists

hello_engine prototyped this surface inline (phases 1031/1034) and
the adversarial review of that code caught a GPU use-after-free in
the naive buffer-growth path — destroying an outgrown VB mid-frame
while frame N-1 still reads it. The fix (park outgrown buffers and
destroy them only after `frame_idx + 3`, the engine's TLAS-ring
convention) is exactly the kind of policy that must live in ONE
place. Promoting the prototype to a library makes the safe policy
the only path for every consumer (sample overlays today, the editor
viewport next).

## Usage

```cpp
#include <cd/debug_draw/DebugDraw.hpp>

// Boot — formats must match the pass you flush inside.
cd::debug_draw::RendererDesc dd {};
dd.color_attachment_formats = my_pass_formats;
dd.depth_attachment_format  = cd::rhi::Format::kD32Float;
// Optional: MRT passes supply a fragment shader writing every
// attachment; single-target passes can use the embedded default.
dd.fragment_glsl = my_mrt_line_fs;
auto renderer = cd::debug_draw::Renderer::create(device, compiler, dd);

// Per frame — append anywhere, flush LAST in the colour pass.
batch.add_aabb(bmin, bmax, {0, 1, 0, 1});
renderer->flush(device, cmd, batch, view_proj, frame_idx);
batch.clear();

// Shutdown (device idle).
renderer->destroy(device);
```

## Design rules

- **Frames-in-flight-safe growth.** Outgrown vertex buffers are
  parked and reclaimed only after `kDestroyMargin (= 3)` frames; a
  compile-time test pins the margin to the engine convention so it
  cannot drift silently.
- **Depth test ON, depth write OFF.** Lines occlude correctly behind
  geometry but never punch holes into the depth buffer for later
  passes.
- **Shader contract.** Default embedded shaders write one colour
  attachment; consumers with MRT passes pass `fragment_glsl` (and
  optionally on-disk hot-reload paths — ADR-20260529-X5 precedence).
- **Move-only RAII-ish.** `destroy(device)` releases GPU handles
  under the device-idle contract (same as `cd::material::Material`).

## Tests

`tests/test_debug_draw.cpp` — NullDevice-based: creation error
contract (GLSL-only + no compiler fails gracefully), invalid-renderer
flush no-op, move semantics, destroy-margin convention pin.
Pixel-level verification lives with consumers (hello_engine golden
fixtures draw every 3D overlay through this renderer).
