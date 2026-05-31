# cd::post

**Purpose**: post-processing effects suite. Implements tone-mapping, bloom, temporal anti-aliasing (TAA), screen-space reflections (SSR), depth-of-field, motion blur, SMAA, GTAO, and composite operations.

**Namespace**: `cd::post_*` (separate namespaces per effect: `post_bloom`, `post_taa`, `post_tonemap`, etc.).

**Headers**: `cd/post/{bloom,taa,ssr,composite,gtao,tonemap,camera,dof,motion_blur,smaa}/*.hpp`.

**Primary types** (varies per sub-library):
- `cd::post_bloom::BloomPass` -- multi-scale bloom pass.
- `cd::post_taa::TaaPass` -- temporal anti-aliasing with Halton jitter.
- `cd::post_ssr::SsrPass` -- screen-space reflections.
- `cd::post_tonemap::TonemapPass` -- HDR-to-SDR tone-mapping.
- `cd::post_composite::CompositePass` -- layer compositing with blend modes.

**Sub-libraries**:
- `cd::post_bloom` -- Bloom HDR effect.
- `cd::post_taa` -- Temporal anti-aliasing.
- `cd::post_ssr` -- Screen-space reflections.
- `cd::post_gtao` -- Ground-truth ambient occlusion.
- `cd::post_tonemap` -- Tone-mapping algorithms (ACES, Filmic, etc.).
- `cd::post_composite` -- Multi-pass compositing.
- `cd::post_dof`, `cd::post_motion_blur`, `cd::post_smaa`, `cd::post_camera` -- Additional effects.

**Usage example**:
```cpp
#include <cd/post/bloom/BloomPass.hpp>
// Apply bloom during post-fx chain
```

**Test command**: `ctest --preset ninja-debug -R "cd_test_post_.*" --output-on-failure`.

**Notes**:
- Phase 420 consolidated all post-processing sub-libraries.
- Shader sources in respective `shaders/` subdirectories.

**TODO**: expand coverage (currently <3 test cases).
