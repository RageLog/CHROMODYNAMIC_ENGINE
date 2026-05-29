# cd::post_bloom

**Purpose**: multi-mip HDR bloom chain (Karis-stable prefilter / 13-tap downsample / 9-tap tent upsample). Three full-screen-triangle shaders share the cd::post_composite::kCompositeVS so the bloom chain reuses one VS pipeline-cache key across the prefilter + sample + upsample passes.

**Namespace**: `cd::post_bloom`.

**Headers**: `cd/post_bloom/Bloom.hpp`.

**Primary types**:
- `cd::post_bloom::kPrefilterFS` -- HDR-input to mip0; subtracts threshold via Karis-stable knee (avoids fireflies).
- `cd::post_bloom::kDownsampleFS` -- 13-tap downsample (Sledgehammer / Jimenez); next mip = blur of previous.
- `cd::post_bloom::kUpsampleFS` -- 9-tap tent upsample with additive blend; sum accumulates onto the previous mip.
- `cd::post_bloom::PrefilterPush` -- { threshold, knee, soft_knee, intensity } push constant (16 B).
- `cd::post_bloom::UpsamplePush` -- { strength, hdr_factor } push constant matching kUpsampleFS layout.

**Usage**:
```cpp
#include <cd/post_bloom/Bloom.hpp>

cd::material::MaterialDesc bp {};
bp.vertex_glsl = cd::post_composite::kCompositeVS;
bp.fragment_glsl = std::string_view { cd::post_bloom::kPrefilterFS };
bp.color_attachment_formats = { cd::rhi::Format::kRGBA16Float };
// fill PrefilterPush per frame, dispatch full-screen tri into mip 0
```

**Test command**: `ctest --preset ninja-debug -R cd_test_post_bloom --output-on-failure`.

**Notes**:
- Mip chain count + base size are caller-driven; hello_engine uses a 6-mip chain at swapchain extent.
- Upsample uses additive blend (BlendOp::kAdd, BlendFactor::kOne both src + dst) so up-chain accumulates without an explicit ping-pong.
- Threshold knee follows the Karis SIGGRAPH 2013 talk; prevents fireflies in HDR PBR scenes.
