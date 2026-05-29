# cd::velocity

**Purpose**: per-mesh screen-space velocity G-buffer pass. Re-rasterises every entity with a tiny VS+FS pair that writes `(curr_uv - prev_uv)` into an RG16F target. Downstream consumers: composite motion-blur, TAA reprojection, future denoiser feedback.

**Namespace**: `cd::velocity`.

**Headers**: `cd/velocity/Velocity.hpp`.

**Primary types**:
- `cd::velocity::kVelocityVS` -- vertex shader. Reads `prev_vp_model` + `curr_vp_model` (128-byte push) plus position only from the vertex stream; outputs both screen positions.
- `cd::velocity::kVelocityFS` -- fragment shader. Writes `(curr_uv - prev_uv)` to location 0.
- Push layout: `mat4 prev_vp_model + mat4 curr_vp_model`, stages = kVertex only.

**Usage**:
```cpp
#include <cd/velocity/Velocity.hpp>

cd::material::MaterialDesc md {};
md.vertex_glsl = std::string_view { cd::velocity::kVelocityVS };
md.fragment_glsl = std::string_view { cd::velocity::kVelocityFS };
md.color_attachment_formats = { cd::rhi::Format::kRG16Float };
md.depth_attachment_format = cd::rhi::Format::kD32Float;
md.depth_stencil.depth_test = true;
md.depth_stencil.depth_write = false;        // read-only depth
md.depth_stencil.depth_compare = cd::rhi::CompareOp::kLessEqual;
```

**Test command**: `ctest --preset ninja-debug -R cd_test_velocity --output-on-failure`.

**Notes**:
- Per-mesh velocity (proper for object motion + camera motion); composite motion-blur reads the gbuf_velocity target every frame.
- Depth-write off + LessEqual compare means the velocity pass only writes where the HDR pass already won the depth test; preserves the original z buffer.
- TAA in cd::post_composite uses this to reproject history into current-frame UV, eliminating ghosting on fast camera moves.
