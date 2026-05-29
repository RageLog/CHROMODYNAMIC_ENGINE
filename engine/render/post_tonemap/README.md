# cd::post_tonemap

## Purpose
State-of-the-art tonemap operators for linear-to-display conversion: Narkowicz ACES, Hill ACES, Hable (Filament), and AGX. Provided as header-only CPU functions and matching GLSL strings for runtime compilation.

## Namespace
`cd::post_tonemap::` — all public symbols.

## Public headers
- `Tonemap.hpp` — Tonemap operator implementations (C++ + GLSL)

## Primary types
- Tonemap operator enum (Narkowicz, Hill, Hable, AGX)
- CPU implementations of each operator
- GLSL string constants for shader compilation

## Usage example
```cpp
#include <cd/post_tonemap/Tonemap.hpp>

// CPU-side
vec3 display_color = cd::post_tonemap::tonemap_hable(linear_rgb);

// GPU-side (via runtime GLSL)
// #include "tonemap.glsl"
// vec3 color = tonemap_aces(linear_rgb);
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_post_tonemap
```

## Test
```bash
ctest --preset ninja-debug -R post_tonemap
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types
- `cd::math` — Vector math

## Notes
- Header-only library
- Integrates with post_taa, post_bloom, and final composite
- Day 13 research phase
- Part of cd::post_fx umbrella

## References
- Narkowicz et al. — ACES tone mapper
- Hable 2010 — Filament tone mapping reference
- AGX tone mapper (Jed Smith) research
