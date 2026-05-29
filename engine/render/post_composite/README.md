# cd::post_composite

**Purpose**: full-screen composite pass that takes the HDR scene + G-buffers + post-fx inputs (bloom, AO, SSR, motion-blur, shafts, TAA history, vignette, chromab, film-grain) and produces the final swapchain LDR colour + TAA next-frame history.

**Namespace**: `cd::post_composite`.

**Headers**: `cd/post_composite/Composite.hpp`.

**Primary types**:
- `cd::post_composite::kCompositeVS` / `kCompositeFS` -- GLSL string-views for the full-screen triangle composite shaders.
- `cd::post_composite::Push` -- push-constant struct (exposure, saturation_boost, bloom_post, ao_strength, dof_strength, shafts_strength, ssr_strength, motion_blur, taa_amount, fog/chromab/grain/vignette params; matches the kCompositeFS layout 1:1).
- The shaders MRT-write 2 targets: location 0 = swapchain BGRA8 LDR, location 1 = next-frame TAA history (BGRA8).

**Usage**:
```cpp
#include <cd/post_composite/Composite.hpp>

cd::material::MaterialDesc md {};
md.vertex_glsl = cd::post_composite::kCompositeVS;
md.fragment_glsl = cd::post_composite::kCompositeFS;
md.color_attachment_formats = { swap_fmt, history_fmt };
// fill push, descriptor bindings, etc.
```

**Test command**: `ctest --preset ninja-debug -R cd_test_post_composite --output-on-failure`.

**Notes**:
- Carries the tonemap (Hable filmic by default) + saturation pull. Previously tonemap lived inline in prim/PBR FS; moved into composite at R3 phase 219 so all post-fx see linear HDR.
- TAA ping-pong: composite reads history[frame & 1], writes history[(frame & 1) ^ 1]. main() owns the per-frame swap.
- hello_engine R-Showcase panel writes Push fields live (HelloEngineFx.hpp aggregate).
