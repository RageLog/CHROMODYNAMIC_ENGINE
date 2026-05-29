# cd::post_ssr

**Purpose**: screen-space reflections via depth + normal raymarch. Hierarchical-Z accelerated ray traversal with thickness rejection + falloff towards screen edges. Provides reflection contribution for shiny surfaces without RT acceleration structures.

**Namespace**: `cd::post_ssr`.

**Headers**: `cd/post_ssr/{Settings,Raymarch,HiZ}.hpp`.

**Primary types**:
- `cd::post_ssr::Settings` -- { strength, max_steps, thickness, fade_edge_dist }.
- `cd::post_ssr::raymarch_screen_space(uv, normal, view_dir) -> hit_uv` -- shader-side ray traversal.
- `cd::post_ssr::HiZ` -- hierarchical-Z mip chain CPU baker (compute-shader bake queued).

**Test command**: `ctest --preset ninja-debug -R cd_test_post_ssr --output-on-failure`.

**Notes**:
- Falls back to env-cube IBL when SSR rejects the hit (off-screen / behind / too far).
- hello_engine R-Showcase exposes `fx.ssr_strength` (0.5F default, visible on metallic spheres + the wet floor).
- W8-AY chrome-mirror parameters preserved alongside SSR for the sphere-grid demo.
