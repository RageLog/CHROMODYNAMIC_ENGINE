# cd::hdr_display

## Purpose
HDR display and color space conversion utilities. Supports tone mapping, gamut mapping, and HDR10 / scRGB output pipelines for modern displays and streaming targets.

## Namespace
`cd::hdr_display::` — all public symbols.

## Public headers
- HDR display modes, color space converters, tone map operator integration

## Primary types
- Display mode descriptor; color space transform; tone map output format

## Usage example
```cpp
#include <cd/hdr_display/HdrDisplay.hpp>

// Query display capabilities
auto display = cd::hdr_display::query_display_hdr();
if (display.supports_hdr10) {
  // Tone map to HDR10 container
  auto out = cd::hdr_display::tonemap_to_hdr10(linear_color);
}
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_hdr_display
```

## Test
```bash
ctest --preset ninja-debug -R hdr_display
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types
- `cd::math` — Color space math

## Notes
- Header-only library
- Integrates with post_tonemap for final output stage
- Platform-specific HDR query via cd::platform

## References
- SMPTE ST 2084 (PQ) and ST 2086 (metadata) standards
- Khronos HDR display extension specs
