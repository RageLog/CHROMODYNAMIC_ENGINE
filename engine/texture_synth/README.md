# cd::texture_synth

## Purpose

Procedural texture generators for runtime synthesis without asset
loading: 2D value-noise hash with fBm composition + Earth-like
albedo + normal + metallic-roughness packing. Useful for
placeholders, dynamic detail maps, and the bake-time procedural
textures `cd::ibl` uses when no HDR file is supplied.

## Namespace

`cd::texture_synth`

## Public headers

- `include/cd/texture_synth/Noise.hpp` — hash21 + value_noise2 +
  fbm2 (cubic Hermite, 3 octaves) + phase898 value_noise2_quintic +
  fbm2_quintic_6oct (Perlin 2002 improved smoothing, 6 octaves —
  mirror of the runtime cloud-overlay shader's quality).
- `include/cd/texture_synth/Earth.hpp` — Earth-like albedo / normal
  / metallic-roughness palettes for the procedural fallback used by
  hello_engine's pre-bound `albedo_tex` / `normal_tex` / `mr_tex`
  when no glTF is loaded.

## Primary functions

- `hash21(x, y) -> float` — 2-integer hash → uniform [0, 1].
- `value_noise2(u, v, freq) -> float` — cubic-Hermite value noise.
- `fbm2(u, v, base_freq) -> float` — 3-octave fBm on value_noise2.
- `value_noise2_quintic(u, v, freq) -> float` — Perlin 2002
  quintic smoothing; eliminates the axis-aligned ridges cubic
  value-noise leaves at cell boundaries.
- `fbm2_quintic_6oct(u, v, base_freq) -> float` — 6-octave fBm
  on the quintic core; same shape as the cloud shader's `cd_fbm4`
  (which became 6 octaves in phase853).
- `bake_earth_*()` — see Earth.hpp.

## Usage example

```cpp
#include <cd/texture_synth/Noise.hpp>

// 512×512 detail-noise float buffer.
std::vector<float> heights(512 * 512);
for (int y = 0; y < 512; ++y)
    for (int x = 0; x < 512; ++x)
        heights[y * 512 + x] =
            cd::texture_synth::fbm2_quintic_6oct(
                x / 512.0F, y / 512.0F, 4.0F);
```

## Build / Test

```bash
cmake --build --preset ninja-debug --target cd_texture_synth
ctest --preset ninja-debug -R texture_synth
```

## Dependencies

- `cd::core` — engine types (Defines).

## References

- **Perlin 2002**, "Improving Noise" — quintic Hermite smoothing
  that powers `value_noise2_quintic` (and the GLSL `cd_value_noise`
  the cloud overlay uses).
- **Ebert et al. 2003**, "Texturing & Modeling: A Procedural
  Approach" — fBm composition.

## Notes

- Header-only; safe to include in shader-side CPU mirrors.
- Deterministic — same `(x, y, freq)` always returns the same noise
  value, no global state.
- The quintic-noise helpers landed in phase 898 (Run 24) as the
  CPU mirror of the cloud overlay's GLSL quality bump. Older
  cubic helpers stay around for callers that don't need
  ridge-free output (faster constants on the cubic path).
</content>
