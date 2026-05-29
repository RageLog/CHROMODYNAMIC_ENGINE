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
