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

## phase691 — composite DDGI + ReSTIR hooks (M14 W4)

`cd::post_composite` exposes two optional consumer hooks that wire the
full GI/RT chain (`cd::ddgi::FullPipeline` from phase 680 + Sprint-5,
`cd::restir_di::FullPipelineDenoised` from phase 681 + Sprint-6) into
the composite pass.

**Defaults (phase730 — FINALE-1 W2 A6):**

- `CD_COMPOSITE_USE_DDGI`   — still **default OFF** (A5 owns the DDGI flip).
- `CD_COMPOSITE_USE_RESTIR` — flipped to **default ON**: denoised RT
  direct illumination now lives in every default render, giving
  temporally-stable RT shadows without a manual flag flip.

### Flags

| CMake option              | Effect when ON                                                                        |
| ------------------------- | ------------------------------------------------------------------------------------- |
| `CD_COMPOSITE_USE_DDGI`   | Composite samples DDGI indirect irradiance (`binding 6`) and adds it to lit RGB.      |
| `CD_COMPOSITE_USE_RESTIR` | Composite samples ReSTIR denoised direct light (`binding 7`) and adds it to lit RGB. |

The CMake options propagate to every consumer of `cd::post_composite`
via `target_compile_definitions(... INTERFACE ...)`. They also feed the
GLSL preprocessor when the composite FS source is built through
`cd::post::composite::make_composite_fs_source_with_gi_hooks(...)`.

### Binding contract

When `CD_COMPOSITE_USE_DDGI` is ON the composite FS expects
`sampler2D cd_ddgi_indirect_irradiance` at `set = 0, binding = 6`. The
view must reference the output image of
`cd::ddgi::FullPipeline::bind_sample_resources(...)` — a viewport-sized
RGBA16F containing per-pixel indirect diffuse irradiance.

When `CD_COMPOSITE_USE_RESTIR` is ON the composite FS expects
`sampler2D cd_restir_denoised_direct` at `set = 0, binding = 7`. The
view must reference the SVGF chain final output of
`cd::restir_di::FullPipelineDenoised::execute(...)` — a viewport-sized
RGBA16F containing denoised direct-light radiance.

Both contributions add in **linear HDR** to the lit colour after the
analytic-light forward pass + bloom and **before** exposure / tonemap.
The renderer is responsible for SKIPPING analytic lights whose direct
contribution ReSTIR is now responsible for (otherwise the direct
illumination is double-counted). The DDGI hook is purely additive on
top of the existing ambient / IBL term — no other pass needs to gate.

### Helper

```cpp
#include <cd/post/composite/Composite.hpp>
#include <cd/shader/Compiler.hpp>

// Build FS source with both macros prepended; consumes the same string
// the C++ code sees via #ifdef.
const std::string fs_src =
    cd::post::composite::make_composite_fs_source_with_gi_hooks(
        /*use_ddgi=*/true, /*use_restir=*/true);

cd::shader::CompileDesc fsd {};
fsd.source = fs_src;
fsd.stage  = cd::shader::ShaderStage::kFragment;
auto fs_spv = compiler->compile(fsd);  // -> SPIR-V ready for create_shader_module()
```

When both `use_ddgi` and `use_restir` are `false` the helper returns
`kCompositeFSWithGiHooks` unchanged → SPIR-V byte-equivalent to the
no-flag baseline.

### Tests

- `cd_test_post_composite` — static layout / GLSL source-string sanity
  (no Vulkan).
- `cd_test_composite_ddgi_restir_hook` — **Vulkan-gated**. Forces both
  flags ON locally (test-private `target_compile_definitions`), compiles
  the GI-hook FS via glslang, creates a graphics pipeline with the
  8-binding descriptor set layout (HDR + bloom + depth + G-buf normal +
  history + velocity + DDGI + ReSTIR), writes all 8 descriptors, and
  draws a single fullscreen triangle into a 2-MRT RGBA8 colour pair.
  Validation layers active throughout → any binding mismatch, image
  layout error, or SPIR-V/layout incompatibility fails the test. Skips
  cleanly when no Vulkan ICD is present.

### Moment

A graphics dev flips `CD_COMPOSITE_USE_DDGI=ON` in their CMake,
recompiles, and the scene gets actual indirect bounce lighting in the
next frame — the full GI/RT chain is consumable, not just dispatchable.

**TODO**: expand coverage (currently <3 test cases).
