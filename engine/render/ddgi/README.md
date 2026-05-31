# cd::ddgi

Dynamic Diffuse Global Illumination via probe-based irradiance fields (Majercik et al.,
JCGT 8:2, 2019). Maintains a 3-D grid of probes that trace rays in compute (via
`GL_EXT_ray_query` + `cd_tlas`) to update octahedral irradiance and visibility atlases,
enabling real-time diffuse GI for moving geometry and dynamic lights.

## Namespace

`cd::ddgi`

## Public header

`include/cd/ddgi/Ddgi.hpp` — header-only; all CPU math + GLSL source strings.

## Key types

| Type | Purpose |
| --- | --- |
| `ProbeGrid` | 3-D lattice; default 8×4×8, unit spacing. `probe_world_pos`, `probe_index_from_world`, `flat_index`. |
| `ProbeAtlas` | Atlas texture layout (irradiance rgba16f + visibility rg16f). `init_from_grid`, `probe_uv`. |
| `TraceSettings` | `rays_per_probe` (64), `hysteresis` (0.97), `max_distance` (20 m). |
| `IrradianceField` | Aggregate: grid + atlas + settings + `sky_color` + `default_ambient`. `ambient_fallback`. |

## CPU math functions

| Function | Description |
| --- | --- |
| `octahedral_encode(Vec3f)` | Unit sphere → UV ∈ [0,1]² (Cigolle et al. 2014). |
| `octahedral_decode(Vec2f)` | UV → unit sphere; round-trip error < 0.5° at 8×8 face. |
| `probe_world_pos(grid, px, py, pz)` | Grid-coord → world-space centre. |
| `trilinear_probe_weights(grid, p, weights, corners)` | 8-corner weights + coords for `p`. |

## GLSL compute / fragment sources

| Constant | Shader stage | Purpose |
| --- | --- | --- |
| `kDdgiTraceCS` | Compute | Ray-trace from each probe via `cd_tlas` (`GL_EXT_ray_query`). |
| `kDdgiBlendIrradianceCS` | Compute | Low-discrepancy EMA blend → irradiance atlas. |
| `kDdgiBlendVisibilityCS` | Compute | Chebyshev visibility (mean depth, depth²) → visibility atlas. |
| `kDdgiSampleFS` | Fragment | Per-fragment trilinear + Chebyshev-gated indirect sample. |

## Usage example

```cpp
#include <cd/ddgi/Ddgi.hpp>

// Build an irradiance field for a 20 m cube scene.
cd::ddgi::IrradianceField field;
field.grid.origin  = { -10.0F, 0.0F, -10.0F };
field.grid.spacing = {   2.5F, 2.5F,   2.5F };
field.sky_color    = { 0.3F, 0.5F, 1.0F };
field.init();   // populates atlas dimensions

// Query 8 nearest probe indices for a shading point.
std::array<float, 8> weights{};
auto indices = field.grid.probe_index_from_world({3.0F, 1.0F, -2.0F}, weights);

// Encode a surface normal for atlas UV lookup.
auto uv = cd::ddgi::octahedral_encode({0.0F, 1.0F, 0.0F});

// Sky fallback when no probes have hit geometry this frame.
auto ambient = field.ambient_fallback(/*any_probes_hit=*/false);
```

## Build / Test

```bash
cmake --build --preset ninja-debug --target cd_ddgi
ctest --preset ninja-debug -R ddgi --output-on-failure
```

## Dependencies

- `cd::core` — engine defines and types
- `cd::math` — `Vec2f`, `Vec3f`

## GPU wiring (phase527+)

1. Bind `cd_tlas` acceleration structure + ray_radiance/ray_dir_dist images.
2. Dispatch `kDdgiTraceCS` — one workgroup per probe (8×8 threads = 64 rays).
3. Dispatch `kDdgiBlendIrradianceCS` + `kDdgiBlendVisibilityCS` — one workgroup
   per probe face texel.
4. Bind irradiance + visibility atlases to the composite pass FS and inject
   `kDdgiSampleFS` logic into `cd::post::composite`.

## References

- Majercik, Z., Marrs, A., Spjut, J., McGuire, M. (2019). *Dynamic Diffuse Global
  Illumination with Ray-Traced Irradiance Fields*. JCGT 8(2).
  [jcgt.org/published/0008/02/01](https://jcgt.org/published/0008/02/01/)
- Cigolle, Z. et al. (2014). *A Survey of Efficient Representations for Independent
  Unit Vectors*. JCGT 3(2).
- McGuire, M. et al. (2017). *Real-Time Global Illumination Using Precomputed Light
  Field Probes*. I3D 2017.
- ADR: `docs/ADR/ADR-20260531-ddgi.md`
