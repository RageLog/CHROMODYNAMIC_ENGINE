# cd::restir_gi

## Scope (honest — BAND-7 sealed)

This library is the **reservoir-resampling-math-v1** core of ReSTIR GI
(Ouyang 2021). It ships:

- the correct WRS streaming update, the cross-pixel reservoir **combine**
  (RIS estimator), the history **clamp**, and the temporal **age / blend** —
  CPU reference (header-only) + matching GLSL helper strings;
- three embedded compute-shader strings (sample / temporal-reuse /
  spatial-reuse) that exercise that reservoir math end-to-end.

**GI is NOT actually traced here.** The sample kernel (`kRestirGiSampleCS`)
fills each candidate with a clearly-marked **DEFERRED-TRACE** hit (a
synthesised bounce point + a constant cached radiance) instead of issuing a
ray query against the scene TLAS. A real GI gather (ray-query + radiance
cache, driven by a render-graph consumer following the
`cd::restir_di::DispatchPass` pattern — 4 reservoir SSBOs, 3 compute
pipelines, descriptor sets, a TLAS binding) is a substantial RHI-dispatch
subsystem and is a **promote-on-need** item, not a gap in this header's
stated scope. See `docs/ADR/ADR-20260616-band7-scope.md` §1.

So: **reservoir-math-v1 = done + tested; GI-trace = deferred-by-design.**

## Namespace
`cd::restir_gi::`

## Public header
- `include/cd/restir_gi/GiReservoir.hpp` — `Sample`, `Reservoir`,
  `update` / `combine` / `clamp_history` / `temporal_blend`, plus the GLSL
  helper + compute-kernel strings.

## Primary types
- `cd::restir_gi::Sample` — one indirect-bounce sample (hit point + normal +
  cached incoming radiance + visibility marker).
- `cd::restir_gi::Reservoir` — WRS reservoir (survivor + `weight_sum` + `M` +
  `age`); `final_weight(target_pdf)` is the RIS unbiased denominator.

## Usage example
```cpp
#include <cd/restir_gi/GiReservoir.hpp>

cd::restir_gi::Reservoir r {};
// Stream candidate bounces through the WRS update.
cd::restir_gi::update(r, candidate, weight, rng01());
// Spatial / temporal reuse: combine a neighbour's reservoir, re-evaluating
// its target PDF at the receiver geometry.
cd::restir_gi::combine(r, neighbour, neighbour_pdf, rng01(),
    [&](const cd::restir_gi::Sample& s) { return eval_target_pdf(s); });
// Cap history so disocclusion artefacts don't linger.
cd::restir_gi::clamp_history(r, 20U);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_restir_gi
ctest --preset ninja-debug -R restir_gi
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector math

## References
- **Ouyang, Liu, Toth, Ramani, Whaley, Lefohn 2021**, "ReSTIR GI: Path
  Resampling for Real-Time Path Tracing" (HPG 2021).
- **Bitterli et al. 2020**, "Spatiotemporal reservoir resampling for
  real-time ray tracing with dynamic direct lighting" (SIGGRAPH / TOG 39:4)
  — the DI sibling whose reservoir algebra this mirrors.

## Notes
- Header-only (INTERFACE) CPU reference + GLSL kernel strings.
- The reservoir math is byte-for-byte the same shape as `cd::restir_di`, so
  the promote-on-need GI trace can reuse that library's proven dispatch path.
- Pairs with a denoiser (`cd::denoise`) once the real trace lands.
