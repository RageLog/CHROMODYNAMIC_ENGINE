# cd::restir_di

## Purpose
ReSTIR direct illumination — weighted reservoir sampling (WRS) for per-pixel light source selection with temporal and spatial reuse. Reduces variance in multi-light stochastic shadow sampling by orders of magnitude with minimal overhead.

## Namespace
`cd::<render>::restir_di::`

## Public headers
- `include/cd/restir_di/Reservoir.hpp` — WRS sample container and combine semantics
- `include/cd/restir_di/ReuseStrategy.hpp` — Temporal/spatial reuse patterns
- `include/cd/restir_di/DispatchPass.hpp` — GPU initial-candidate + temporal + spatial reuse compute pass (Sprint-1, Sprint-2)
- `include/cd/restir_di/Denoiser.hpp` — A-trous wavelet edge-aware blur (Sprint-3, legacy hookpoint)
- `include/cd/restir_di/SvgfDenoiser.hpp` — SVGF skeleton: moment / variance / filter passes as independently-configurable building blocks (Sprint-4)
- `include/cd/restir_di/FullSvgfPipeline.hpp` — **Sprint-5** named full SVGF chain: moment + variance + 3x A-trous filter as a single dispatch entry; production seam the integrator wires into the framegraph composite
- `include/cd/restir_di/FullPipelineDenoised.hpp` — **Sprint-6** full denoised pipeline: sample -> temporal_reuse -> spatial_reuse -> SVGF in one `execute(...)` call; the production seam the integrator wires into the framegraph composite for noise-free direct illumination

## Primary types
- `Restir_di::Reservoir` — Selected light index + sample weight + validity flags
- `Restir_di::ReusePattern` — Neighbor offset sets for spatial taps
- `Restir_di::SvgfDenoiser` — three-pass skeleton with per-knob configure (variance, depth_phi, normal_phi, temporal_alpha, filter_iterations)
- `Restir_di::FullSvgfPipeline` — production wrapper, fixed `filter_iterations = 3`; argument order matches the framegraph G-buffer slot order `(reservoir, normal, depth, mesh_id, out)`
- `Restir_di::FullPipelineDenoised` — Sprint-6 facade: owns one DispatchPass + one FullSvgfPipeline; one configure() + one execute() per frame; advances `frame_index()` after each successful execute

## SVGF Pipeline (Sprint-5, chained)

`cd::restir_di::FullSvgfPipeline` orchestrates the three SVGF passes
(Schied 2017, "Spatiotemporal Variance-Guided Filtering: Real-Time
Reconstruction for Path-Traced Global Illumination") as a single named
dispatch:

1. **Moment estimation pass** — reads the per-pixel reservoir luminance,
   accumulates first + second raw moments into a temporal SSBO with an
   exponential alpha decay, increments a `history_length` counter (capped
   at 64).
2. **Variance estimation pass** — when `history_length <
   kSvgfShortHistoryThreshold` (4 by default), falls back to a 7×7
   bilateral spatial luminance variance estimate (paper Section 4.1);
   otherwise uses the temporal moment difference `mu_2 - mu_1^2`.
3. **Edge-aware A-trous filter pass** — 5×5 wavelet stencil run 3 times
   (`kFullSvgfFilterIterations`) with step-width doubling per iteration.
   The luminance edge-stop sigma is variance-derived per paper Eq. 5:
   `phi_l = phi_color * sqrt(g(variance))` with `g` a 3×3 Gaussian
   prefilter over the variance SSBO. The filter ping-pongs between the
   input reservoir buffer and the output buffer; with 3 iterations the
   final write lands on the output buffer (iterations 0 and 2 write
   out, iteration 1 writes back to input).

```cpp
#include <cd/restir_di/FullSvgfPipeline.hpp>

cd::restir_di::FullSvgfPipeline pipe;
auto cr = pipe.configure(*device, viewport_w, viewport_h);
// ... per-frame record:
pipe.execute(cmd_buf,
             reservoir_buf,            // ReSTIR DI spatial-reuse output
             gbuffer_normal_view,
             gbuffer_depth_view,
             gbuffer_mesh_id_view,
             denoised_reservoir_buf);  // framegraph composite input
```

The depth / normal / mesh-id texture views are **reserved seam slots** at
Sprint-5 -- the integrator currently passes null handles and the kernel
falls back to a reservoir-luminance-only edge stop (Sprint-3-equivalent).
Sprint-6 will wire the G-buffer slots so depth + normal edge stops kick
in and the chain matches the Schied 2017 reference exactly.

## Full Denoised Pipeline (Sprint-6, chained)

`cd::restir_di::FullPipelineDenoised` strings together every library-level
pass into a single `execute(...)` call -- the production seam the
integrator wires into the framegraph composite for noise-free direct
illumination:

1. **sample** (Sprint-1, `DispatchPass::record`) -- streams `candidates`
   (Bitterli 2020 `M_initial`, default 32) initial WRS candidates per
   pixel into the current-frame reservoir SSBO.
2. **temporal reuse** (Sprint-2, `DispatchPass::execute_temporal_reuse`) --
   blends the current-frame reservoir with the previous-frame survivor;
   writes the temporally-blended output SSBO.
3. **spatial reuse** (Sprint-2, `DispatchPass::execute_spatial_reuse`) --
   5-tap disc kernel; writes the spatial-reuse output SSBO (which is the
   SVGF chain's input).
4. **SVGF** (Sprint-4/5, `FullSvgfPipeline::execute`) -- moment +
   variance + 3x A-trous filter; writes the caller-supplied `out_tex`.

```cpp
#include <cd/restir_di/FullPipelineDenoised.hpp>

cd::restir_di::FullPipelineDenoised pipe;
cd::restir_di::FullPipelineDenoisedConfig cfg {};
cfg.viewport_width  = viewport_w;
cfg.viewport_height = viewport_h;
cfg.light_count     = scene.light_count();
cfg.candidates      = 32; // Bitterli 2020 default
auto cr = pipe.configure(*device, cfg);
// ... per-frame record:
pipe.execute(cmd_buf,
             cd::restir_di::SceneLightView{},  // Sprint-7 seam
             gbuffer_depth_view,
             gbuffer_normal_view,
             gbuffer_mesh_id_view,
             denoised_reservoir_buf);          // framegraph composite input
```

The pipeline advances a monotonic `frame_index()` after each successful
execute() so the on-GPU PCG seed varies frame-to-frame across the reuse
passes. The `SceneLightView` + depth / normal / mesh-id texture views are
**reserved seam slots** at Sprint-6 (the sub-pass kernels still run the
Sprint-2/3/4 stripped paths -- reservoir-luminance fallback); Sprint-7
will wire the scene-light SSBO + G-buffer slots through the framegraph
G-buffer ABI so the chain matches the production reference exactly.

## Usage example
```cpp
#include <cd/restir_di/Reservoir.hpp>

// Initialize per-pixel reservoir.
cd::restir_di::Reservoir reservoir;

// Stream multiple random light samples, update WRS.
for (int i = 0; i < num_candidate_lights; ++i) {
  float weight = evaluate_light(lights[i], current_pixel);
  reservoir.update(lights[i].id, weight, rng());
}

// Retrieve final selected light.
int selected_light = reservoir.selected_idx();
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_restir_di
ctest --preset ninja-debug -R restir_di
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Bitterli et al. 2020**, "Spatiotemporal Reservoir Resampling for Real-Time Ray Tracing with Dynamic Direct Lighting" (SIGGRAPH)
- ADR: `docs/ADR/ADR-*-restir-di-*.md`

## Notes
- Header-only CPU reference + GLSL compute shader helpers.
- Dramatically improves convergence in denoised ray tracing (denoise layer absorbs temporal jitter).
- Supports temporal reuse (history from prior frame) and spatial reuse (neighbor consensus).
- Integrates with multi-bounce path tracing via cd::restir_gi.
