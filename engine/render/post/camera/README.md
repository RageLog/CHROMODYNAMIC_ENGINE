# cd::post_camera

## Purpose
Inline camera and composition post-effects: vignette, chromatic aberration, and film grain. Provided as header-only GLSL helper strings and Settings structures.

## Namespace
`cd::post_camera::` — all public symbols.

## Public headers
- `PostCamera.hpp` — Settings structs and GLSL helper definitions

## Primary types
- `VignetteSettings` — Falloff radius and intensity
- `ChromaticAberrationSettings` — RGB channel shift amounts
- `FilmGrainSettings` — Grain intensity and color saturation
- GLSL string constants for each effect

## Usage example
```cpp
#include <cd/post_camera/PostCamera.hpp>

cd::post_camera::VignetteSettings vignette{radius: 0.8, intensity: 0.5};
cd::post_camera::ChromaticAberrationSettings ca{shift: 0.01};

// Apply in GLSL shader via embedded helpers
```

## Build
```bash
cmake --build --preset ninja-debug --target cd_post_camera
```

## Test
```bash
ctest --preset ninja-debug -R post_camera
```

## Dependencies (per CMakeLists)
- `cd::core` — Core types

## Notes
- Header-only library
- GLSL sources embedded as string constants
- Lightweight effects suitable for inline composite pass
- Part of cd::post_fx umbrella

## References
- Common camera post-effect techniques in real-time rendering
