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
- **Multi-set pipelines (phase 864)**: `MaterialDesc::extra_set_layouts` carries an optional span of caller-owned `DescriptorSetLayoutHandle` values. Each entry becomes a descriptor set in the pipeline layout AFTER the material's own descriptor set (which always sits at set index 0). Used by hello_engine's W8-BE bindless dedicated-set path so a bindless sampler2D array can live on its own descriptor set, isolated from the shared per-prim set that NVIDIA's driver fails to dynamic-index correctly. Caller owns the layouts; Material does NOT take ownership.

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

## RT closest-hit sample contract (T1.12)

The chrome PBR sphere bug surfaced in the 2026-06-03 user screenshot
(`docs/AUDIT/learned-lessons-pbr-rt-and-curtain-alpha-2026-06-03.md`, Bug A)
revealed a second blind spot: when an RT reflection ray hits a non-sphere
prim (Sponza wall, curtain, vegetation), the closest-hit shader has no way
to read the prim's `MaterialInstance` albedo / emissive — so the reflection
falls back to black or to the IBL miss branch, and the chrome sphere
appears to reflect only sky + nearby spheres.

The full engine-owned RT closest-hit shader is deferred behind an inline-
GLSL-string boundary (today the closest-hit lives in
`samples/rhi/hello_rt/main.cpp` and `hello_engine` is FROZEN). What this
library ships in phase 657 is the **stable CPU-side contract** the future
shader-record builder will read:

```cpp
#include <cd/material/Material.hpp>

cd::material::MaterialInstance inst = /* per-prim, glTF-loaded */;
inst.set_albedo(0.10F, 0.65F, 0.20F);    // Sponza curtain green
inst.set_emissive(0.0F, 0.05F, 0.0F);    // optional LED trim
inst.set_metallic(0.0F);
inst.set_roughness(0.85F);

const auto sample = cd::material::ray_hit_sample(inst);
// sample.albedo[3], sample.emissive[3], sample.metallic, sample.roughness,
// sample.valid — packed into the per-prim SBT albedo SSBO by the future
// engine RT pipeline. Closest-hit GLSL reads:
//
//   const vec3 albedo = albedo_buf[gl_InstanceCustomIndexEXT].rgb;
//   payload.color = albedo * lighting + emissive;
```

### Defensive fallback

When the `MaterialInstance` is inert (default-constructed, or device
handles released) `ray_hit_sample` returns `valid = false` AND fills
`albedo` with the neutral-grey constant `kRayHitFallbackGrey = 0.6`.
Emissive falls back to zero so the fallback path cannot accidentally
light the scene.

The 0.6 magnitude is high enough to read as a real diffuse reflection
against an HDR sky background, low enough to look like an unresolved
stand-in instead of a lit surface. Without this fallback a partially-
wired material would render the chrome reflection as black, which is
indistinguishable from a miss — the user would see "no geometry in the
chrome" instead of "the geometry is there but its colour is missing".

### Future RT closest-hit integration point

When the engine RT pipeline lands (post-X6, when Vulkan + D3D12 closest-
hit shader records are owned by `cd::render::rt_pipeline` instead of a
sample), the integration is:

1. The shader-record builder iterates the scene's per-prim
   `MaterialInstance` list and calls `ray_hit_sample(*inst)` for each.
2. The result is packed into a per-prim SSBO indexed by
   `gl_InstanceCustomIndexEXT` — same indexing pattern as
   `samples/rhi/hello_rt/main.cpp` already demonstrates.
3. The closest-hit GLSL reads the buffer on every hit and shades
   `payload.color` from `albedo + emissive + Schlick(metallic,roughness)`
   instead of returning black / IBL miss.
4. The branch is gated on `hit_kind != kSphere` so the existing chrome /
   sphere code paths stay byte-identical (no M0 regression).

The unit-test target `cd_test_rt_hit_general_geometry` locks in the
contract: case 1 (valid glTF prim → non-zero sample.albedo), case 2
(inert instance → neutral-grey fallback). Future RT integration work
re-runs this test before touching downstream shader code.
