# ADR-20260531: ReSTIR DI/GI — Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing

**Date**: 2026-05-31
**Status**: Implemented (phase527)
**Stakeholders**: Rendering, Real-Time Ray Tracing, Performance

---

## Bağlam

Path-traced direct illumination with one sample per pixel (1-spp) produces unacceptably noisy
images when the scene has many lights. Traditional approaches — uniform light sampling, light
trees, visibility caches — either scale poorly with light count or require expensive precomputation
that breaks with dynamic lights.

The engine already has an RHI abstraction (cd::rhi), a DXR/VK raytracing pipeline, and per-pixel
G-Buffer data. The missing piece is an **efficient light-selection strategy** that amortises sample
cost across pixels and frames without breaking temporal coherence.

Requirements:
1. Support 1000+ dynamic lights without O(N) per-light shadow rays.
2. Temporal stability — no ghosting or lag artefact beyond ~20 frames.
3. Unbiased (or controllably biased) estimator with provable convergence.
4. CPU-side reference implementation for unit-testing the math independently of the GPU pipeline.
5. Composable with the existing ReSTIR GI bounce-reuse path (cd::restir_gi).

---

## Kandidatlar

| Approach | Convergence | Dynamic lights | Memory | Notes |
|---|---|---|---|---|
| Uniform light sampling (1-spp) | Poor | Yes | Minimal | Baseline; too noisy |
| Light BVH / lightcuts (Walter 2006) | Good | Partial | Medium | Rebuild cost on dynamic geometry |
| Importance-sampled env map (Talbot 2005) | Good | Env only | Low | Inapplicable to scene lights |
| **ReSTIR DI (Bitterli 2020)** | **Excellent** | **Yes** | Per-pixel reservoir | **Selected** |
| Neural cache (NRC, Müller 2021) | Excellent | Yes | High (training) | Inference latency budget unclear |

For **indirect illumination** (second-bounce and beyond):

| Approach | Notes |
|---|---|
| Path-traced DDGI (Majercik 2019) | Probe grid; breaks in open/outdoor scenes |
| RTXGI 2.0 (probe + cache) | Vendor-specific infrastructure required |
| **ReSTIR GI (Ouyang 2021)** | Reservoir per pixel, same estimator as DI | **Selected** |

---

## Karar

**ReSTIR DI** (Bitterli et al., SIGGRAPH 2020 / TOG 39:4) is adopted as the primary
direct-illumination sampling strategy for real-time ray tracing in CHROMODYNAMIC.

**ReSTIR GI** (Ouyang et al., HPG 2021) is adopted as the primary indirect-bounce
resampling strategy.

Both share the **Weighted Reservoir Sampling (WRS)** estimator defined in Algorithm 1 of
Bitterli 2020 and differ only in the type of sample they carry:

- DI: `(light_index, radiance, target_pdf)` — selecting which light to shadow-test.
- GI: `(secondary_hit_point, normal, cached_incoming_radiance)` — selecting which bounce path to reuse.

### Core estimator (Bitterli Eq. 3–6)

Given M candidate samples `x_i` drawn from proposal distribution `q`, streaming WRS
selects a survivor with probability proportional to the **target distribution** `p_hat`:

```
w_i = p_hat(x_i) / q(x_i)          // resampling weight
W   = (1 / p_hat(y)) * (1/M) * sum(w_i)   // unbiased contribution weight (Eq. 6)
```

Combining two reservoirs R_a (M_a samples) and R_b (M_b samples) is equivalent to
running WRS over M_a + M_b candidates, enabling O(1) spatial and temporal reuse.

### Temporal carry buffer

Each pixel keeps a **ring of N = 1 reservoir** per frame (the current frame's reservoir)
plus the **previous frame's reservoir** accessed via reprojection. The temporal blend
uses a history cap M_cap = 20 * M_new (Bitterli §4.3) to bound stale-history bias.

The host-side `TemporalBuffer<R>` (cd/restir_di/TemporalBuffer.hpp) holds a double-buffered
flat array indexed by pixel, swapped per frame. Reservoirs older than `max_age` frames are
invalidated (weight_sum → 0, M → 0).

### Spatial reuse

A 5-tap screen-space neighbourhood kernel (radius ≤ 30 px, randomised rotation per frame)
feeds the `combine()` primitive. Each tap re-evaluates `p_hat` in the receiver's domain
(Bitterli §5) to avoid bias from parallax.

### GLSL compute pipeline

Three compute shader string_views are exposed from the DI header as `constexpr std::string_view`:

| Name | Purpose |
|---|---|
| `kRestirDiSampleCS` | Initial candidate generation — stratified light pick + WRS per pixel |
| `kRestirDiTemporalReuseCS` | 1-frame reservoir combine with M cap |
| `kRestirDiSpatialReuseCS` | 5-tap neighbourhood combine + final weight output |

The GI equivalent exposes `kRestirGiSampleCS`, `kRestirGiTemporalReuseCS`, `kRestirGiSpatialReuseCS`
from `GiReservoir.hpp`.

---

## Reddedilen Alternatifler

**Light BVH (lightcuts)**: Build cost O(N log N) per frame for dynamic lights, and the
cut-traversal per ray requires coherent wavefront execution that is hard to achieve in
deferred shading mode.

**Neural radiance cache**: Promising for static scenes; current inference budget (< 0.5 ms)
requires bespoke CUDA kernels outside of our vendor-agnostic RHI layer. Revisit in Phase 3
once the RHI supports shader-side ML inference (SM 9.0+ cooperative vectors).

**DDGI probe grid**: Performs well indoors but the probe density required for outdoor Sponza /
open-world scenarios is prohibitive without a dynamic probe scheduler — a separate multi-sprint
system not in scope for Phase 2.

---

## Sonuçlar

1. `cd::restir_di` and `cd::restir_gi` are promoted from stub to **reference-complete**
   header-only libraries. Both expose CPU-testable WRS math and GLSL compute shader strings.
2. A new global test binary `test_restir_math` (6 cases) validates the WRS estimator,
   reservoir merge, temporal blend, and invalidation on age overflow without GPU involvement.
3. The `TemporalBuffer<R>` template (cd/restir_di/TemporalBuffer.hpp) provides the
   double-buffered pixel-indexed ring used by both DI temporal and GI temporal passes.
4. Bias control: the unbiased variant requires a visibility ray per spatial tap.
   The biased (no reuse visibility) path is the default to keep the budget predictable;
   the caller selects via `ReSTIRConfig::unbiased_spatial = true/false`.
5. Performance target: DI pass ≤ 2 ms at 1080p / 4096 lights on RTX 4080 class hardware.
   GI pass ≤ 3 ms same conditions (additional world-space G-Buffer fetch per tap).
6. Integration point: `hello_engine` demo will enable both passes once the `cd::rhi`
   shader-dispatch plumbing (Phase 134 task X6-RT) is wired.

---

## Referanslar

- Bitterli B., Wyman C., Pharr M., Shirley P., Lefohn A., Jarosz W. (2020).
  "Spatiotemporal reservoir resampling for real-time ray tracing with dynamic direct lighting."
  *ACM Transactions on Graphics (TOG)*, 39(4), Article 148. https://doi.org/10.1145/3386569.3392481

- Ouyang Y., Liu S., Toth M., Ramani S., Whaley S., Lefohn A. (2021).
  "ReSTIR GI: Path resampling for real-time path tracing."
  *Proceedings of High-Performance Graphics (HPG 2021)*. https://doi.org/10.2312/hpg.20211281

- Talbot J., Cline D., Egbert P. (2005). "Importance resampling for global illumination."
  *Rendering Techniques (EGSR 2005)*, 139–146.

- Walter B., Arbree A., Bala K., Greenberg D. P. (2006). "Multidimensional lightcuts."
  *ACM SIGGRAPH 2006*, 25(3), 1081–1088.
