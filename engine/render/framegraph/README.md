# cd::framegraph

**Purpose**: lightweight RAII helpers for the most common render-pass targets (color / depth) plus the declarative pass-topology graph used by the engine higher-level frame-graph. Sits beside `cd::rhi` so most samples can build offscreen targets without buying into the full FrameGraph DAG.

**Namespace**: `cd::framegraph`.

**Headers**: `cd/framegraph/{Targets,FrameGraph,PassTopology}.hpp`.

**Primary types**:
- `cd::framegraph::ColorTarget` -- RAII wrapper { TextureHandle image, TextureViewHandle view, Extent2D extent, Format } with `destroy(device)` for explicit teardown.
- `cd::framegraph::DepthTarget` -- same pattern for depth/stencil attachments.
- `cd::framegraph::create_color_target` / `create_depth_target` -- factory functions that allocate plus view with the standard ColorAttachment+Sampled+Storage / DepthStencilAttachment+extra_usage usage masks.
- `cd::framegraph::FrameGraph` -- declarative pass-topology DAG (used by libraries that opt into transient resource lifetime + pass barrier inference).

**Usage**:
```cpp
#include <cd/framegraph/Targets.hpp>

cd::framegraph::ColorTarget hdr {};
if (!cd::framegraph::create_color_target(device, { 1920, 1080 },
                                          cd::rhi::Format::kRGBA16Float, hdr))
{
    return 1;
}
hdr.destroy(device);
```

**Test command**: `ctest --preset ninja-debug -R cd_test_framegraph --output-on-failure`.

**RT phase ordering invariant**:

When using ray-tracing passes in the frame graph:

- **TLAS (Top-Level Acceleration Structure) rebuild passes MUST be scheduled before any RT trace pass in the same frame.**
- The frame graph compiler enforces this via topological sort on RT-pass declared inputs: if a trace pass declares the TLAS handle as input, the compiler ensures the rebuild pass is scheduled strictly before.
- If a trace pass is added without the TLAS handle in its inputs (a developer mistake), the framegraph will silently render black reflections / miss data. Register the TLAS as a read input to enable compile-time detection.
- In debug builds, the framegraph emits a compile-time `CHROMA_ASSERT` to catch misordering: `CHROMA_ASSERT(rebuild_node_id < trace_node_id)`.

**Notes**:
- Header-only for Targets/FrameGraph core.
- The two helpers (ColorTarget / DepthTarget) are the bread-and-butter API for samples and most engine subsystems; the full FrameGraph is only used by libraries opting into declarative pass topology (planned: post-fx, GI).
- hello_engine bundles 7 instances of these into `cd_sample::RenderTargets` for its HDR-MRT layout (samples/engine/hello_engine/HelloRenderTargets.hpp).
