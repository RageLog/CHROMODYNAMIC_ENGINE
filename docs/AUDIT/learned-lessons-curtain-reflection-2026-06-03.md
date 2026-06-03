# Learned Lessons — Sponza Curtain Reflection Bug (User Hand-Fix)

**Date**: 2026-06-03
**Source**: User manually fixed `samples/engine/hello_engine/shaders/prim.frag.glsl` + `main.cpp` (phase629). hello_engine itself remains FROZEN under marathon agent policy — this doc extracts the engine-applicable lessons for cd::render code so future marathons don't recreate the same architectural mistake.

---

## The bug, in one paragraph

Sponza curtains (cloth, metallic ≈ 0) rendered with a faint reflective sheen because the SSR (screen-space reflection) bucket gate was keyed on **material kind** (`is_gltf_prim` → surface_flag = 0.6 → half-strength SSR). The correct gate is **material metallic** — non-metallic surfaces of any kind should get surface_flag = 0 and skip SSR. Compounding that: glTF alphaMode MASK / BLEND was not honored, so transparent curtain edges rendered as opaque silhouettes.

## Two distinct engine-level lessons

### Lesson 1 — SSR / RT-reflection gating must read metallic from the G-buffer, not infer it from material kind

**Where this matters in the engine**:

- `cd::render::post_ssr` (if it exists; otherwise the SSR shader strings inside `cd::render::post_composite`) — the SSR dispatch should read the metallic component from the G-buffer per pixel and gate the trace + composite by `metallic >= kSsrMetallicThreshold` (suggested 0.05).
- `cd::material::MaterialInstance` — needs an explicit `metallic` accessor that the G-buffer fill pass writes into the material channel. Currently the GLSL inlines a hardcoded 0.6 surface_flag whenever the bucket index says "Lit glTF prim", which is the bug pattern at engine level too.
- `cd::framegraph` — when a downstream pass reads `surface_flag`, it should treat it as a continuous metallic-driven weight, not a discrete kind-bucket. Document this contract on the `SurfaceFlagSlot` (or equivalent) channel.

**Concrete code shape** the engine should adopt:

```cpp
// In the G-buffer fill / material eval pass, OUTPUT METALLIC EXPLICITLY:
g_buffer.metallic_roughness = vec2(material.metallic, material.roughness);

// In the SSR / RT-reflection-blend pass:
float metallic = texture(gbuf_metallic_roughness, uv).x;
float ssr_weight = smoothstep(0.05, 0.30, metallic);  // ramp, not binary
if (ssr_weight < 0.01) discard;  // skip the trace entirely for dielectrics
// (or equivalent CPU-side: skip pixel in indirect-dispatch tile)
```

Two follow-up items for a future marathon backlog:

- **T1.8 SSR-metallic-gate ramp**: replace the kind-bucket surface_flag with a metallic-driven `ssr_weight` channel in `cd::render::post_ssr` and downstream composite.
- **T1.9 G-buffer metallic-roughness MRT contract**: ensure `cd::material::MaterialInstance` writes metallic into a dedicated G-buffer channel and the contract is documented in `engine/render/material/README.md`.

### Lesson 2 — glTF alphaMode (MASK + BLEND) must round-trip through the loader, material instance, and the shader

**Where this matters in the engine**:

- `cd::asset::gltf` loader currently extracts albedo / metallic / roughness factors and textures, but does not expose `alphaMode` + `alphaCutoff` on the loaded material descriptor. The shader has no way to discover that a glTF asset wants MASK behavior.
- `cd::material::MaterialInstance` needs `alpha_mode` (enum: kOpaque, kMask, kBlend) + `alpha_cutoff` (float, default 0.5).
- The standard material shader path (whatever cd::render::material::ui_variant or the Lit variant uses) needs the matching alpha-discard branch:

```glsl
if (mat.alpha_mode == ALPHA_MODE_MASK) {
    float a = texture(albedo_tex, uv).a * mat.base_color.a;
    if (a < mat.alpha_cutoff) discard;
}
// (BLEND mode: pipeline state alpha-blend on, no discard)
```

Follow-up:

- **T1.10 glTF alphaMode end-to-end**: extend the loader → material → shader chain so alphaMode + alphaCutoff round-trip without sample-side patches. This is exactly the fix the user shipped inline in hello_engine; the lesson is to move it into the engine libraries.

### Lesson 3 — Template-functor decoupling for per-kind material overrides

**Pattern observed**: `draw_floor_and_entities` previously took a concrete `const std::vector<GltfPrimRange>&` parameter, which baked the Sponza-only path into the template. The user-side fix replaced it with a `PrimRangesFor` functor `(PrimitiveKind) -> span<const GltfPrimRange>`, so the Sponza and CesiumMan paths converge through one lambda per kind.

**Engine-level application**: `cd::render::scene_ingest` or whatever owns the per-primitive material override path should expose a callback-shaped API. The render pass machinery should never hardcode "this is Sponza-only" — every kind that needs a custom material override goes through the same functor signature.

---

## What changed in the user commit (phase629) — engine code does NOT change

User's manual fix lives entirely inside `samples/engine/hello_engine/`. Under the project policy hello_engine is FROZEN for agent work; the user is allowed to hand-modify it. The three lessons above are tracked for **engine-side** implementation in a future marathon (M9 or M10 candidate work), at which point the equivalent fix can also flow back to apps/editor + new samples WITHOUT touching hello_engine again.

## Cross-references

- ADR-20260530-ui-widget-library — for cd::material::MaterialInstance API
- `engine/render/material/README.md` — should grow a "G-buffer channels" section
- `engine/asset/gltf/README.md` — should grow an "alphaMode round-trip" section
- M8 phase628 ships the DspFx Reverb Sprint-2 (the *other* user-driven commit this turn) — unrelated but landed in the same hand-edit batch.
