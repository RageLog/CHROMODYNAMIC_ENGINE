# cd::denoise

## Purpose
Path-tracing image denoiser with two backends: edge-aware a-trous wavelet (Dammertz 2010; no external dependencies) and OpenImageDenoise slot (Intel; opt-in via CD_ENABLE_OIDN). CPU + GLSL implementations.

## Namespace
`cd::denoise::` — all public symbols.

## Public headers
- `Denoise.hpp` — Denoiser interface and filter implementations

## Primary types
- `AtrousFilter` — Edge-aware a-trous wavelet denoiser (Dammertz 2010); CPU + GLSL
- `OidnFilter` — OpenImageDenoise backend slot (requires CD_ENABLE_OIDN)

## Usage example
```cpp
#include <cd/denoise/Denoise.hpp>

// Create a-trous denoiser
auto denoiser = cd::denoise::create_atrous_filter(
  /*color_buffer=*/noisy_image,
  /*normal_buffer=*/normal_map,
  /*passes=*/3
);

// Denoise in-place
denoiser->apply();
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
