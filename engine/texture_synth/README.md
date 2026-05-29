# cd::texture_synth

## Purpose
Procedural texture generators for runtime synthesis without asset loading: 2D value-noise hash with fBm composition, Earth-like albedo palettes, height-to-normal map derivatives, and packed metallic-roughness-AO textures. Useful for placeholders, dynamic detail maps, and infinite terrain.

## Namespace
`cd::<asset>::texture_synth::`

## Public headers
- `include/cd/texture_synth/ValueNoise.hpp` — 2D hash and interpolation
- `include/cd/texture_synth/Fbm.hpp` — Fractional Brownian motion composition
- `include/cd/texture_synth/EarthAlbedo.hpp` — Palette-based Earth colormaps
- `include/cd/texture_synth/Normal.hpp` — Height-to-normal derivatives
- `include/cd/texture_synth/Packing.hpp` — Metallic/Roughness/AO interleaving

## Primary types
- `TextureSynth::ValueNoise` — Hash function, lacunarity, persistence
- `TextureSynth::Fbm` — Octave count, amplitude schedule
- `TextureSynth::EarthAlbedo` — Height-to-color mapping (snow/rock/grass)
- `TextureSynth::NormalMap` — Height derivatives with edge handling

## Usage example
```cpp
#include <cd/texture_synth/Fbm.hpp>
#include <cd/texture_synth/Normal.hpp>

// Generate 512×512 height map via fBm.
cd::texture_synth::Fbm fbm{
  .octaves = 8,
  .lacunarity = 2.0f,
  .persistence = 0.5f
};

std::vector<float> heights(512 * 512);
for (int y = 0; y < 512; ++y) {
  for (int x = 0; x < 512; ++x) {
    heights[y * 512 + x] = fbm.sample(x / 64.0f, y / 64.0f);
  }
}

// Convert to normals.
auto normals = cd::texture_synth::height_to_normal(heights, 512, 512, scale);
```

## Build/Test
```bash
cmake --build --preset ninja-debug --target cd_texture_synth
ctest --preset ninja-debug -R texture_synth
```

## Dependencies
- `cd::core` — engine types
- `cd::math` — vector/matrix math

## References
- **Perlin 2002**, "Improving Noise" (improved Perlin noise)
- **Ebert et al. 2003**, "Texturing & Modeling: A Procedural Approach"

## Notes
- Header-only library (can be used in shaders via GLSL port).
- Deterministic seeding for reproducible infinite worlds.
- CPU-side generation; GPU version via compute shaders in cd::rhi.
- Common in prototyping, terrain editors, and material preview panels.
