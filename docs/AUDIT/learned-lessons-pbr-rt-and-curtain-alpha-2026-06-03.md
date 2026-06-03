# Learned Lessons — PBR Sphere RT Reflection + Curtain Alpha Bleed-Through

**Date**: 2026-06-03
**Source**: User-reported visual bugs in hello_engine Sponza scene (screenshot evidence). hello_engine remains FROZEN per agent policy — engine-side action items are queued for a future marathon. **Feature shipping stays primary; these are queued investigations for when convenient.**

---

## Bug A — PBR chrome sphere reflection does NOT show Sponza

**Symptom**: The chrome PBR sphere (surface_flag = 0.85, has W8-BC RT reflection bucket per phase445) renders with reflections, but Sponza geometry (walls, curtains, vegetation, characters) is absent from the reflection. The sphere reflects something else (sky / IBL / nearby spheres) but not the surrounding Sponza scene.

**Diagnosis hypothesis** (without touching hello_engine):

1. **BLAS/TLAS coverage gap**: When the RT acceleration structure is built per-frame, the Sponza prim instances likely are NOT being added to the TLAS that the PBR sphere's closest-hit probe traverses. Either:
   - Sponza prims are gated out of the RT TLAS by a `should_include_in_rt(kind)` filter that excludes `kGltf`.
   - Sponza prims use a per-instance flag (e.g. `RAY_FLAG_CULL_OPAQUE`) that excludes them from the chrome's reflection trace.
2. **Hit-shader limitation**: The closest-hit shader may only sample IBL/sky on miss + sphere on hit, with no "general geometry" hit branch. Walls + curtains hit the BLAS but the shader returns black / IBL fallback.
3. **TLAS rebuild ordering**: Sponza may be added to TLAS after the chrome sphere's compaction snapshot is taken, so the chrome trace uses a stale TLAS.

**Engine-side action items** (future marathon backlog):

- **T1.11 RT TLAS coverage audit**: in `cd::rhi::ICommandBuffer::build_acceleration_structure`, the TLAS spec must accept ALL geometry kinds that have non-zero `tlas_eligible` flag. Document the contract in `engine/render/rhi/README.md`. Add a unit test that asserts a glTF prim with default flags ends up in the TLAS instance list.
- **T1.12 RT hit-shader general-geometry branch**: the cd::material RT closest-hit shader (or the equivalent shader-record table) must have a branch that samples the hit material's albedo/normal/emissive when ANY geometry (not just spheres) is hit. Today the chrome reflection probably only knows how to render spheres / sky.
- **T1.13 BLAS-build phase ordering invariant**: document in `engine/render/framegraph/README.md` that TLAS rebuild happens BEFORE any RT trace pass each frame, never after. Add a debug-build assert.

**Effort**: L (each item is 1-2 weeks of focused work). Not Sprint-1 priority since hello_engine is reference-only — but matters for apps/editor + future samples that consume cd::material's RT path.

---

## Bug B — Curtains show characters/objects behind them (alpha bleed-through)

**Symptom**: Looking at the green / blue curtains in the screenshot, the curtain pattern texture renders, but vegetation/objects positioned BEHIND the curtain show through as if the curtain were ~50% transparent. The previous user fix (phase629) addressed alpha-cutout edges (MASK mode discard), but the curtain interior alpha values appear to be sub-1.0 across the whole panel, not just at edges.

**Diagnosis hypothesis** (without touching hello_engine):

1. **Wrong alphaMode for curtain assets**: Sponza curtains may use `alphaMode = BLEND` (semi-transparent) when they should be `alphaMode = MASK` (binary cutout) — the artist authored them as BLEND to let the embroidery pattern show, but that also makes the solid cloth area sub-1.0 alpha.
2. **Albedo texture alpha channel is 0.5-0.9 on solid areas**: the texture itself was authored with non-1.0 alpha across the whole cloth, perhaps to enable subtle backlight effects. Without a proper opaque-pass override, those become see-through.
3. **Two-pass ordering issue**: BLEND prims drawn before opaque vegetation behind them get z-tested OUT, so vegetation renders ON TOP of curtain. The fix is to render alpha-blend prims AFTER all opaque geometry.

**Engine-side action items** (future marathon backlog):

- **T1.14 cd::asset::gltf alphaMode interpretation policy**: when loading Sponza, the loader should default to `kOpaque` for any material whose alpha texture channel statistical histogram shows ≥ 95% values at 1.0 (with cutout on the rare < 1.0 pixels). Add a heuristic `infer_alpha_mode(texture_alpha_histogram)` helper.
- **T1.15 Two-pass alpha render order**: `cd::render::scene_ingest` or framegraph pass ordering must split opaque vs alpha-blend prims into separate passes. Opaque first (Z-sorted front-to-back for early-Z), then alpha-blend (sorted back-to-front for correct over-blending). Add `enum class RenderBucket { kOpaque, kAlphaMask, kAlphaBlend }` to drive pass ordering.
- **T1.16 cd::material::MaterialInstance::is_blend()** accessor: explicit query so the render path can route to the right pass.

**Effort**: M (each item ~1 week). T1.15 has cross-cutting impact (framegraph + scene_ingest + material) so it's the highest leverage of the three.

---

## How this enters the plan

This document is the **queued lesson** equivalent of `docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md` from the previous user hand-fix. Items T1.11–T1.16 should be added to `docs/MARATHON_PLAN_NEXT.md` under the rendering tier, marked as **deferred / future marathons** with priority **MEDIUM** (feature shipping stays primary per current user direction).

The next marathon (M10 or wherever these land) should:
1. Pick whichever subset fits the budget — they are loosely-coupled so partial progress is fine.
2. Validate each fix at engine library level (lib tests pass + apps/editor renders correctly when the relevant codepath is exercised) — do NOT re-validate via hello_engine modifications.
3. Once T1.14/T1.15 land, the curtain bleed-through in hello_engine *might* fix itself when the next render rebuild picks up the new pass ordering — but the user will validate that on their hand, not us.

## Related documents

- `docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md` — prior round (SSR metallic gate + glTF alphaMode loader → T1.8/T1.9/T1.10). M9 ships those.
- `docs/AUDIT/editor-black-screen-2026-05-31.md` — apps/editor Submitter gate. T0.4 Route A queued.
- `docs/MARATHON_PLAN_NEXT.md` — main plan. Should grow T1.11–T1.16 entries after M9 closes.
