# cd::asset::texture_compress

Offline texture compression pipeline. Compresses raw RGBA8 pixels into
GPU-native block-compressed formats:

| Family  | Formats                  | Target                          |
|---------|--------------------------|---------------------------------|
| Desktop | BC1 / BC3 / BC5 / BC7    | Windows / Linux / macOS Intel   |
| Mobile  | ASTC 4×4 / 8×8           | Android / iOS / Apple Silicon   |

**Moment**: the game ships with an 8 GB → 2 GB texture footprint;
the mobile build runs on devices without 8 GB VRAM.

## Sprints

| Sprint | Phase | Surface |
|--------|-------|---------|
| 1 | `phase650` | API surface + BC1 stub encoder (naive min/max endpoint picker). |
| 2 | `phase750` | Real BC7 via `bc7enc_rdo`, ASTC via ARM `astc-encoder`, both FetchContent + optional. |

Both Sprint-2 encoders are **optional**. If the network or CMake
FetchContent fails, the library still compiles with the Sprint-1 BC1
encoder; `encode_bc7()` / `encode_astc()` return `std::nullopt` and
`CD_TC_HAS_BC7ENC` / `CD_TC_HAS_ASTCENC` are `0`. The 3-tier
dependency resolution pattern matches `cd::asset::save_compression`
(lz4) and `cd::asset::audio_streamer` (Opus).

### 3-tier resolution

```
                    bc7enc_rdo                  astc-encoder
  Tier 1 vcpkg      ✗ (not in registry)         ✗ (not in registry)
  Tier 2 FetchContent  github.com/richgel999/   github.com/ARM-software/
                       bc7enc_rdo (HEAD)         astc-encoder (4.7.0)
  Tier 3 absent     CD_TC_HAS_BC7ENC=0          CD_TC_HAS_ASTCENC=0
```

## Public surface

```cpp
namespace cd::asset::texture_compress {

enum class Format : uint8_t {
    kBC1, kBC3, kBC5, kBC7,         // desktop
    kAstc4x4, kAstc8x8              // mobile
};

struct CompressedTexture
{
    Format                       format;
    uint32_t                     width;          // base mip
    uint32_t                     height;         // base mip
    std::vector<uint8_t>         blob;           // mip 0, mip 1, ... linear
};

struct EncodeOptions
{
    Format        target          { Format::kBC1 };
    bool          generate_mips   { false };
    float         quality_0_1     { 0.5F };      // BC7/ASTC quality knob
    bool          srgb            { false };
};

[[nodiscard]] std::optional<CompressedTexture>
                    encode(std::span<const uint8_t> rgba8,
                           uint32_t width, uint32_t height,
                           const EncodeOptions& opts);
}
```

## Format selection guide

| Use case                | Format     | Notes                                       |
|-------------------------|------------|---------------------------------------------|
| Albedo / colour, RGB    | BC1 / BC7  | BC1 4 bpp baseline, BC7 8 bpp high-quality. |
| Albedo / colour, RGBA   | BC3 / BC7  | BC3 cheap, BC7 best-in-class.               |
| Tangent-space normals   | BC5        | Two-channel; reconstruct Z in the shader.   |
| Metallic-roughness AO   | BC5 / BC7  | BC5 if 2-channel only, BC7 for 3+.          |
| Mobile equivalents      | ASTC 4×4 / 8×8 | 8×8 ≈ 2 bpp; suitable for backgrounds. |

## Build / test

The library is **link-tested**; the BC7 / ASTC encode paths are
opt-in via `-DCD_ENABLE_BC7ENC=ON` / `-DCD_ENABLE_ASTCENC=ON`. The
default `ninja-debug` preset enables both — when the FetchContent
sources are on disk the encoder paths compile and `cd_test_tc_*`
runs the round-trip tests; when sources are absent they
`GTEST_SKIP`.

## See also

* `cd::asset::cdtex` — CHROMODYNAMIC texture container that wraps the
  output of this library together with sampler metadata for the
  shipping `.pak`.
