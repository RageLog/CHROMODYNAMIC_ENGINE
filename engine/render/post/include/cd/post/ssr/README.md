# cd::post_ssr

**Purpose**: screen-space reflections — Stachowiak/McGuire hierarchical-depth ray march with blue-noise jitter. Closes the "no specular reflections without RT" gap on hardware that lacks ray_query.

**Namespace**: `cd::post::ssr`.

**Header**: `<cd/post/ssr/Ssr.hpp>`.

**Library type**: header-only INTERFACE — `cd::post_ssr`.

---

## Public API

| Symbol | Purpose |
| --- | --- |
| `cd::post::ssr::Settings` | Trace tunables: `max_steps`, `thickness`, `roughness_max`, `rays_per_pixel`, `min_f0`. |
| `cd::post::ssr::schlick_fresnel(f0, cos_theta)` | Scalar Schlick Fresnel for host-side contribution weighting. |
| `cd::post::ssr::roughness_fade(roughness, settings)` | Roughness-driven fade — 1.0 below `kStart = 0.1`, linear ramp to 0.0 at `roughness_max`. |
| `cd::post::ssr::compute_ssr_weight(metallic)` | **Metallic-driven SSR gate (M9 W3A — T1.8)**. Smoothstep ramp on the metallic channel, see contract below. |
| `cd::post::ssr::kSsrTraceCS` | GLSL compute kernel source string. |

## Contract — SSR is gated by **metallic**, not by material **kind**

> SSR is gated by metallic; non-metallic surfaces receive `ssr_weight = 0` and are skipped entirely.

Curtain-reflection bug (hello_engine phase629, see `docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md` Lesson 1) was caused by keying the SSR bucket gate on material kind — every glTF primitive received `surface_flag = 0.6` and got half-strength SSR, including cloth curtains (metallic ≈ 0). The correct gate is metallic, sampled per-pixel from the G-buffer:

```cpp
const float metallic = sample_gbuffer_metallic(uv);
const float ssr_weight = cd::post::ssr::compute_ssr_weight(metallic);
if (ssr_weight < 0.01F) {
    return;  // skip the trace entirely for dielectrics
}
// otherwise trace and modulate the contribution by ssr_weight
```

**Ramp** (smoothstep, not binary):

| metallic | ssr_weight |
| --- | --- |
| ≤ 0.05 | 0.0 (skip) |
| 0.05 → 0.30 | smoothstep |
| ≥ 0.30 | 1.0 (full SSR) |

Why these thresholds:

- `0.05` keeps typical dielectric F0 (≈ 0.04, plastics / paint / cloth) on the "skip" side.
- `0.30` is below all real conductors (gold/copper/steel sit at metallic = 1.0 in glTF MR workflow), so the full-trace branch is never accidentally entered by a slightly-tinted brushed surface.
- The smoothstep avoids a hard pop on materials that intentionally live in the dielectric/conductor transition band (rusted iron, dirty chrome).

## Downstream consumers

This phase only establishes the helper + contract. Downstream consumers (`cd::post_composite` SSR blend, `cd::render::post_ssr` dispatch tile-skip path, future G-buffer metallic MRT slot) are **not** flipped yet — that is a follow-up (T1.9 G-buffer metallic-roughness MRT contract).

## Tests

```
ctest --preset ninja-debug -R cd_test_post_ssr --output-on-failure
```

Coverage: Schlick Fresnel monotonicity, roughness-fade clamp + monotonic decrease, GLSL kernel non-empty, **metallic-gate ramp at 0 / 0.05 / 0.30 / 0.50**.

## References

- Stachowiak & Uludag 2015 — *Stochastic Screen-Space Reflections* (Frostbite / SIGGRAPH).
- McGuire & Mara 2014 — *Efficient GPU Screen-Space Ray Tracing*.
- Wronski 2014 — half-resolution colour pyramid.
- `docs/AUDIT/learned-lessons-curtain-reflection-2026-06-03.md` — engine-side lessons from the hello_engine phase629 hand-fix.
