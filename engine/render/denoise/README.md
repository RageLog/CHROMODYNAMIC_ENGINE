# cd::denoise

## Purpose
Path-tracing image denoiser with two backends: edge-aware a-trous wavelet (Dammertz 2010; no external dependencies) and OpenImageDenoise slot (Intel; opt-in via CD_ENABLE_OIDN). CPU + GLSL implementations.

## Namespace
`cd::denoise::` — all public symbols.

## Public headers
- `Denoise.hpp` — Denoiser interface and filter implementations

## Primary API (free functions; header-only)
- `denoise_atrous(AuxBuffers, AtrousSettings) -> std::vector<Vec3f>` — edge-aware
  a-trous wavelet denoiser (Dammertz 2010), CPU reference; **fully implemented +
  tested**. The matching GLSL compute kernel is `kAtrousCS`.
- `edge_weight(dc, dn, dz, AtrousSettings)` — the per-tap edge-stopping weight.
- `denoise_oidn(AuxBuffers, OidnFilterKind)` — OpenImageDenoise (Intel) backend
  slot. **Pass-through stub today** (returns the input unchanged) so call sites
  can be written against the final API; the real body lands in
  `cd/denoise/OidnBackend.cpp` when `CD_ENABLE_OIDN` + the OIDN dep are wired
  (see ADR-20260616-band4-render-features-scope §denoise).

## Usage example
```cpp
#include <cd/denoise/Denoise.hpp>

cd::denoise::AuxBuffers aux { width, height,
                              noisy_color, albedo, normal, depth };  // spans
cd::denoise::AtrousSettings settings {};   // iterations = 5 (SVGF default)
std::vector<cd::math::Vec3f> clean = cd::denoise::denoise_atrous(aux, settings);
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_denoise
```

## Test
```bash
ctest --preset ninja-debug -R denoise
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types
- `cd::math` — Vector math, image processing

## Optional
- Intel OpenImageDenoise 2.x (if CD_ENABLE_OIDN enabled)

## Notes
- Header-only library
- A-trous filter included by default (no external deps)
- OIDN is optional via vcpkg manifest or FetchContent
- Pairs with path-traced rendering pipelines

## References
- Dammertz, Sewtz, Hanika, Lensch 2010 — "Edge-Avoiding A-trous Wavelet Transform for Fast Global Illumination Filtering"
- Schied et al. 2017 — SVGF (uses a-trous as spatial pass)
- Intel OpenImageDenoise 2.x documentation
