# cd::material

**Purpose**: high-level Material + MaterialInstance abstraction sitting on top of `cd::rhi::Pipeline`. Bundles GLSL source + descriptor layout + push-constant ranges + raster / depth / blend state into a single `MaterialDesc`; `Material::create(device, compiler, desc)` rolls the compile + pipeline-creation up.

**Namespace**: `cd::material`.

**Headers**: `cd/material/{Material,AnalyticalSkyMaterial,LitPbrMaterial,SkinnedLitMaterial,StandardPbrMaterial,PbrParams,BrdfLut}.hpp`.

**Primary types**:
- `cd::material::MaterialDesc` -- declarative pipeline spec (shaders, attachment formats, descriptor bindings, push constants, raster / depth-stencil / blend state, debug name).
- `cd::material::Material` -- owns the underlying pipeline + descriptor set layout (move-only, RAII).
- `cd::material::MaterialInstance` -- per-frame descriptor set bound to the Material layout. `bind_texture(binding, view, sampler)`, `bind_buffer(binding, handle, offset, size)`, `apply(cmd)`.
- `cd::material::AnalyticalSkyMaterial` / `LitPbrMaterial` / `SkinnedLitMaterial` / `StandardPbrMaterial` -- ready-made shader strings + Push structs for common pipelines.
- `cd::material::PbrParams` -- POD that mirrors the GLSL PBR uniform layout (albedo, metallic, roughness, emissive).

**Usage**:
```cpp
#include <cd/material/Material.hpp>

cd::material::MaterialDesc md {};
md.vertex_glsl = kVS;
md.fragment_glsl = kFS;
md.color_attachment_formats = { cd::rhi::Format::kRGBA16Float };
md.name = "myapp/material/foo";
auto mat = cd::material::Material::create(device, compiler.get(), md);
if (!mat.has_value()) return 1;
mat->apply(cmd);
```

**Test command**: `ctest --preset ninja-debug -R cd_test_material --output-on-failure`.

**Notes**:
- Material is move-only (owns RHI handles); MaterialInstance is move-only and bound 1:1 to a Material layout.
- hello_engine bundles its 7 sample materials into `cd_sample::MaterialBundle` (samples/engine/hello_engine/HelloMaterials.hpp).
- The prebuilt analytical sky + PBR variants are stable references; user code can supply arbitrary GLSL via MaterialDesc.

## G-buffer MRT channel contract (T1.9)

`MaterialInstance` exposes `metallic()` / `roughness()` / `set_metallic(float)` /
`set_roughness(float)` accessors so downstream passes have a stable CPU-side
view of the two scalars that drive Schlick F0 lerp and GGX roughness^2 — the
two knobs SSR / RT-reflection composite must read **per pixel** rather than
infer from a material-kind enum.

The setter pair clamps into `[0,1]`; out-of-range input is silently saturated.
The accessors are pure CPU state. The G-buffer geometry pass and any std140
PbrFactors packer are responsible for forwarding the values to the GPU.

### Channel layout

| MRT slot | Name                                                | Format                                | Channels                                            | Consumed by                                                  |
| -------- | --------------------------------------------------- | ------------------------------------- | --------------------------------------------------- | ------------------------------------------------------------ |
| 0        | `cd_gbuf_albedo`                                    | `RGBA8Unorm`                          | albedo.rgb + alpha-mode bucket                      | composite (direct lighting), SSAO                            |
| 1        | `cd_gbuf_normal`                                    | `RGBA16Float`                         | normal.xyz + `surface_flag` (LEGACY)                | SSR weight (transitioning to slot 2)                         |
| 2        | `cd_gbuf_metallic_roughness`                        | `R8G8Unorm` (V1)                      | `.r = metallic`, `.g = roughness`                   | SSR + RT-reflection composite + denoiser metallic-aware blur |
| 3        | `cd_gbuf_velocity`                                  | `RG16Float`                           | per-pixel motion vector                             | motion blur, TAA reprojection                                |
| 4        | `cd_gbuf_emissive_occlusion`                        | `RGBA8Unorm`                          | emissive.rgb + occlusion                            | composite tonemap input                                      |

V1 uses `R8G8Unorm` (256 buckets per channel) for the metallic_roughness
slot — sufficient for SSR gating and analytical BRDF eval; a future
upgrade to `R16G16Unorm` is reserved for ReSTIR / NRC denoiser passes that
need sub-step roughness discrimination.

### Downstream gate (replaces the surface_flag bucket bug pattern)

The legacy `cd_gbuf_normal.w` `surface_flag` was a **discrete kind bucket**
(0.6 = glTF Lit, 0.85 = chrome, 1.0 = dielectric showcase). The Sponza
curtain bug in phase629 showed this is the wrong abstraction: non-metallic
glTF surfaces inherited `surface_flag = 0.6` and picked up a half-strength
SSR sheen they should not have.

The MRT slot 2 contract replaces that with a **continuous metallic readback**:

```glsl
// SSR / RT-reflection composite — gate by metallic, not by material kind.
float metallic   = texture(cd_gbuf_metallic_roughness, uv).r;
float roughness  = texture(cd_gbuf_metallic_roughness, uv).g;
float ssr_weight = smoothstep(0.05, 0.30, metallic);  // ramp, not binary
if (ssr_weight < 0.01) discard;                       // skip dielectrics
```

Suggested threshold: `kSsrMetallicThreshold = 0.05` — anything below it is
considered fully dielectric (cloth, plaster, wood) and skips the trace.

### Pass producers / consumers

- **Writer**: `cd::render::scene_ingest` G-buffer fill pass. For each draw
  it reads `MaterialInstance::metallic()` / `roughness()` and emits them
  into MRT slot 2. (Wiring lives in T1.9b; this commit ships the API +
  contract only.)
- **Readers**:
  - `cd::render::post_ssr` — gates the screen-space reflection trace by the
    metallic readback (planned in T1.8).
  - `cd::post::composite` — uses metallic to weight the SSR ↔ RT-reflection
    blend and the IBL specular contribution.
  - `cd::render::restir::svgf` — metallic-aware denoiser variance kernel
    will sample slot 2 to bias the filter footprint on smooth metals.

### Tracking

Cross-reference `docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md`
for the full rationale. T1.9 (this commit) ships the API + contract; T1.9b
ships the framegraph wiring; T1.8 ships the metallic-driven SSR gate ramp.
