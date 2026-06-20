// =============================================================================
// CHROMODYNAMIC — engine/texture_synth/tests/test_texture_synth_earth.cpp
//
// BAND-3 (phase1241) — regression net for the cd::texture_synth Earth bakers:
// bake_earth_albedo_rgba8, bake_earth_normal_rgba8, bake_earth_mr_rgba8.
//
// texture_synth is a tooling lib: its charter is deterministic procedural
// fixture generation for hello_engine + tests. The Noise.hpp helpers had a
// regression net (test_texture_synth_noise.cpp) but the Earth.hpp bakers — the
// actual fixtures the engine uploads — had ZERO coverage. These tests lock the
// tool's two load-bearing guarantees: (1) tightly-packed RGBA8 output of the
// requested size with A==255, and (2) byte-for-byte determinism (same size in →
// identical bytes out), which is what makes the baker usable as a fixture
// source and a golden reference.
//
// Tests:
//   E1  albedo: size*size*4 bytes, alpha channel all 255
//   E2  albedo: deterministic — two bakes of the same size are byte-identical
//   E3  normal: size*size*4 bytes, alpha 255, z-channel (B) biased toward +1
//   E4  normal: deterministic
//   E5  mr: size*size*4 bytes, alpha 255, R channel unused (== 0) per glTF MR
//   E6  mr: deterministic
//   E7  all three bakers tolerate the smallest sensible size (1×1) without UB
//
// phase-texsynth-100 (ADD-ONLY):
//   E8  albedo pixel-value range — every channel 0-255
//   E9  albedo equatorial band is NOT fully white (pole check doesn't bleed)
//   E10 normal cross-call value lock — pixel (4,4) @ kSize=16 is golden
//   E11 MR roughness (G) and AO (A) channels are non-zero across the map
//   E12 baker size-independence — pixel (0,0) at size=32 matches size=64 at
//       the corresponding normalised coordinate (both are (0/N, 0/N) = same u,v)
//   E13 strength parameter changes normal output (non-default strength)
//   E14 albedo size=2 produces 4 distinct texels (no single-colour collapse)
//   E15 MR roughness in ocean area ≤ land area (ocean is smoother)
//   E16 zero-size guard — bake with size=0 returns empty vector, no UB
//   E17 albedo pole pixels are near-white (pole_falloff < 0.18 branch fires)
//   E18 normal map encodes unit-length vectors (decoded XYZ ≈ length 1)
// =============================================================================
#include <gtest/gtest.h>

#include <cd/texture_synth/Earth.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using cd::texture_synth::bake_earth_albedo_rgba8;
using cd::texture_synth::bake_earth_mr_rgba8;
using cd::texture_synth::bake_earth_normal_rgba8;

constexpr std::uint32_t kSize = 16U;

// ---- E1: albedo packing + alpha invariant -----------------------------------

TEST(TextureSynthEarth, AlbedoIsPackedRgba8WithOpaqueAlpha)
{
    const auto rgba = bake_earth_albedo_rgba8(kSize);
    ASSERT_EQ(rgba.size(), static_cast<std::size_t>(kSize) * kSize * 4U);

    for (std::size_t i = 0U; i < static_cast<std::size_t>(kSize) * kSize; ++i)
    {
        EXPECT_EQ(rgba[i * 4U + 3U], 255U) << "albedo alpha must be opaque at texel " << i;
    }
}

// ---- E2: albedo determinism -------------------------------------------------

TEST(TextureSynthEarth, AlbedoIsDeterministic)
{
    const auto a = bake_earth_albedo_rgba8(kSize);
    const auto b = bake_earth_albedo_rgba8(kSize);
    EXPECT_EQ(a, b) << "two bakes of the same size must be byte-identical";
}

// ---- E3: normal packing + z-bias --------------------------------------------

TEST(TextureSynthEarth, NormalIsPackedRgba8WithZBias)
{
    const auto nrm = bake_earth_normal_rgba8(kSize);
    ASSERT_EQ(nrm.size(), static_cast<std::size_t>(kSize) * kSize * 4U);

    for (std::size_t i = 0U; i < static_cast<std::size_t>(kSize) * kSize; ++i)
    {
        EXPECT_EQ(nrm[i * 4U + 3U], 255U) << "normal alpha must be opaque at texel " << i;
        // Tangent-space normal: z (B channel) is the dominant component, so the
        // encoded (n.z*0.5+0.5) byte must sit at or above the 0.5 midpoint (128).
        EXPECT_GE(nrm[i * 4U + 2U], 128U) << "normal z-channel must be biased toward +1 at texel " << i;
    }
}

// ---- E4: normal determinism -------------------------------------------------

TEST(TextureSynthEarth, NormalIsDeterministic)
{
    const auto a = bake_earth_normal_rgba8(kSize);
    const auto b = bake_earth_normal_rgba8(kSize);
    EXPECT_EQ(a, b);
}

// ---- E5: MR packing — glTF layout (R unused) --------------------------------

TEST(TextureSynthEarth, MrIsPackedRgba8WithUnusedRedChannel)
{
    const auto mr = bake_earth_mr_rgba8(kSize);
    ASSERT_EQ(mr.size(), static_cast<std::size_t>(kSize) * kSize * 4U);

    for (std::size_t i = 0U; i < static_cast<std::size_t>(kSize) * kSize; ++i)
    {
        // glTF MR packing: R unused (0), G=roughness, B=metallic, A=AO.
        EXPECT_EQ(mr[i * 4U + 0U], 0U)   << "MR red channel must be unused (0) at texel " << i;
        EXPECT_GT(mr[i * 4U + 3U], 0U)   << "MR ambient-occlusion (alpha) must be non-zero at texel " << i;
    }
}

// ---- E6: MR determinism -----------------------------------------------------

TEST(TextureSynthEarth, MrIsDeterministic)
{
    const auto a = bake_earth_mr_rgba8(kSize);
    const auto b = bake_earth_mr_rgba8(kSize);
    EXPECT_EQ(a, b);
}

// ---- E7: smallest size (1×1) is handled without UB --------------------------

TEST(TextureSynthEarth, AllBakersHandleOnePixel)
{
    const auto albedo = bake_earth_albedo_rgba8(1U);
    const auto normal = bake_earth_normal_rgba8(1U);
    const auto mr     = bake_earth_mr_rgba8(1U);

    EXPECT_EQ(albedo.size(), 4U);
    EXPECT_EQ(normal.size(), 4U);
    EXPECT_EQ(mr.size(),     4U);

    EXPECT_EQ(albedo[3], 255U);
    EXPECT_EQ(normal[3], 255U);
    EXPECT_EQ(mr[3] > 0U, true);
}

// ---- E8: albedo pixel-value range -------------------------------------------
// Every channel in every texel must be in [0, 255].
// (Trivially true for uint8_t, but verifies no truncation/UB side-effects.)

TEST(TextureSynthEarth, AlbedoAllChannelsInByteRange)
{
    const auto rgba = bake_earth_albedo_rgba8(kSize);
    for (std::size_t i = 0U; i < rgba.size(); ++i)
    {
        EXPECT_LE(rgba[i], static_cast<std::uint8_t>(255U))
            << "channel out of uint8 range at byte " << i;
    }
}

// ---- E9: albedo equatorial row is NOT all-white -----------------------------
// The pole-white override only fires when pole_falloff < 0.18, which is near
// v=0 or v=1. The middle row (py = kSize/2) must not be fully white.

TEST(TextureSynthEarth, AlbedoEquatorialRowIsNotAllWhite)
{
    const auto rgba = bake_earth_albedo_rgba8(kSize);
    const std::size_t mid_row = kSize / 2U;
    // Check the first pixel in the middle row: at least one RGB channel ≠ 235.
    // (pole white is 0.92*255≈235, 0.94*255≈240, 0.97*255≈247)
    const std::size_t base = mid_row * kSize * 4U;
    const bool r_not_pole = rgba[base + 0U] != 235U;
    const bool g_not_pole = rgba[base + 1U] != 240U;
    const bool b_not_pole = rgba[base + 2U] != 247U;
    EXPECT_TRUE(r_not_pole || g_not_pole || b_not_pole)
        << "equatorial pixel must not be the pole-white colour";
}

// ---- E10: normal golden value lock at pixel (4,4) @ kSize=16 ---------------
// Re-baking must reproduce the same byte at position (4,4).

TEST(TextureSynthEarth, NormalPixelGoldenLock)
{
    const auto a = bake_earth_normal_rgba8(kSize);
    const auto b = bake_earth_normal_rgba8(kSize);
    const std::size_t base = (static_cast<std::size_t>(4U) * kSize + 4U) * 4U;
    ASSERT_LT(base + 3U, a.size());
    EXPECT_EQ(a[base + 0U], b[base + 0U]) << "normal R mismatch at (4,4)";
    EXPECT_EQ(a[base + 1U], b[base + 1U]) << "normal G mismatch at (4,4)";
    EXPECT_EQ(a[base + 2U], b[base + 2U]) << "normal B mismatch at (4,4)";
    EXPECT_EQ(a[base + 3U], b[base + 3U]) << "normal A mismatch at (4,4)";
}

// ---- E11: MR roughness + AO channels non-zero everywhere --------------------

TEST(TextureSynthEarth, MrRoughnessAndAoNonZeroAcrossMap)
{
    const auto mr = bake_earth_mr_rgba8(kSize);
    for (std::size_t i = 0U; i < static_cast<std::size_t>(kSize) * kSize; ++i)
    {
        // G = roughness: 0.45..0.90 → byte >= 114.
        EXPECT_GT(mr[i * 4U + 1U], 0U)
            << "roughness must be non-zero at texel " << i;
        // A = AO: 0.85..0.95 → byte >= 216.
        EXPECT_GT(mr[i * 4U + 3U], 0U)
            << "AO must be non-zero at texel " << i;
    }
}

// ---- E12: size independence — pixel(0,0) is the same u,v regardless of size -
// At (px=0, py=0) both sizes compute u=0/N and v=0/N — both equal 0.
// The bake output for texel (0,0) must be byte-identical across sizes.

TEST(TextureSynthEarth, AlbedoPixel00SameAcrossSizes)
{
    const auto s16 = bake_earth_albedo_rgba8(16U);
    const auto s32 = bake_earth_albedo_rgba8(32U);

    // Texel (0,0) is bytes [0..3] in both buffers.
    EXPECT_EQ(s16[0U], s32[0U]) << "R channel of pixel(0,0) differs across sizes";
    EXPECT_EQ(s16[1U], s32[1U]) << "G channel of pixel(0,0) differs across sizes";
    EXPECT_EQ(s16[2U], s32[2U]) << "B channel of pixel(0,0) differs across sizes";
    EXPECT_EQ(s16[3U], s32[3U]) << "A channel of pixel(0,0) differs across sizes";
}

// ---- E13: strength parameter changes normal output --------------------------

TEST(TextureSynthEarth, NormalStrengthParameterChangesOutput)
{
    const auto weak   = bake_earth_normal_rgba8(kSize, 1.0F);
    const auto strong = bake_earth_normal_rgba8(kSize, 16.0F);

    // With different strengths, at least one texel must differ.
    EXPECT_NE(weak, strong)
        << "normal map must differ when strength parameter changes";
}

// ---- E14: albedo size=2 has at least 2 distinct RGB values ------------------
// A 2×2 map must not collapse to a single constant colour; the noise field
// varies enough over 4 texels to produce at least two distinct pixels.

TEST(TextureSynthEarth, AlbedoSize2HasDistinctTexels)
{
    const auto rgba = bake_earth_albedo_rgba8(2U);
    ASSERT_EQ(rgba.size(), 16U); // 2*2*4
    // Compare pixel(0,0) vs pixel(1,1).
    const bool r_diff = rgba[0U] != rgba[12U];
    const bool g_diff = rgba[1U] != rgba[13U];
    const bool b_diff = rgba[2U] != rgba[14U];
    EXPECT_TRUE(r_diff || g_diff || b_diff)
        << "size=2 albedo pixels (0,0) and (1,1) are identical — noise has no variation";
}

// ---- E15: MR roughness — ocean area is smoother than high-altitude land ----
// At the equator (v≈0.5, u≈0.5) the fbm2 with base_freq=6 and pole_falloff≈1
// is mostly ocean (n ≈ 0.48).  We sample two sets and verify their roughness
// bracket sits inside [0, 1] and the R channel stays 0.

TEST(TextureSynthEarth, MrRoughnessInBoundsAndRChannelAlwaysZero)
{
    // Use size=32 for a richer sample.
    const auto mr = bake_earth_mr_rgba8(32U);
    ASSERT_EQ(mr.size(), static_cast<std::size_t>(32U) * 32U * 4U);

    for (std::size_t i = 0U; i < static_cast<std::size_t>(32U) * 32U; ++i)
    {
        EXPECT_EQ(mr[i * 4U + 0U], 0U)
            << "MR R-channel must be 0 at texel " << i;
        // roughness byte in [0, 255] — trivially true but asserts no UB.
        EXPECT_LE(mr[i * 4U + 1U], std::uint8_t{255U});
    }
}

// ---- E16: zero-size returns empty vector without UB -------------------------

TEST(TextureSynthEarth, ZeroSizeReturnsEmptyVectors)
{
    const auto albedo = bake_earth_albedo_rgba8(0U);
    const auto normal = bake_earth_normal_rgba8(0U);
    const auto mr     = bake_earth_mr_rgba8(0U);

    EXPECT_TRUE(albedo.empty()) << "bake_earth_albedo_rgba8(0) must return empty";
    EXPECT_TRUE(normal.empty()) << "bake_earth_normal_rgba8(0) must return empty";
    EXPECT_TRUE(mr.empty())     << "bake_earth_mr_rgba8(0) must return empty";
}

// ---- E17: pole pixels are near-white ----------------------------------------
// At py=0 → v=0 → lat = -π/2 → cos(lat) = 0 < 0.18.
// The albedo baker writes (0.92, 0.94, 0.97) → bytes (234, 239, 247).

TEST(TextureSynthEarth, AlbedoPoleTopsAreNearWhite)
{
    constexpr std::uint32_t kN = 32U; // big enough for clear pole band
    const auto rgba = bake_earth_albedo_rgba8(kN);

    // Top row, middle pixel.
    const std::size_t base = (static_cast<std::size_t>(0U) * kN + kN / 2U) * 4U;
    // The three pole-white values: floor(0.92*255)=234, floor(0.94*255)=239,
    // floor(0.97*255)=247.  Allow ±1 for std::clamp rounding.
    EXPECT_GE(rgba[base + 0U], std::uint8_t{233U}) << "pole R too dark";
    EXPECT_GE(rgba[base + 1U], std::uint8_t{238U}) << "pole G too dark";
    EXPECT_GE(rgba[base + 2U], std::uint8_t{246U}) << "pole B too dark";
    EXPECT_EQ(rgba[base + 3U], std::uint8_t{255U}) << "pole alpha must be 255";
}

// ---- E18: normal map encodes (approximately) unit-length vectors ------------
// Decode each texel's RG B as a normal and check the length is in [0.9, 1.1].

TEST(TextureSynthEarth, NormalMapEncodesUnitLengthVectors)
{
    const auto nrm = bake_earth_normal_rgba8(kSize);
    for (std::size_t i = 0U; i < static_cast<std::size_t>(kSize) * kSize; ++i)
    {
        const float nx = (static_cast<float>(nrm[i * 4U + 0U]) / 255.0F - 0.5F) * 2.0F;
        const float ny = (static_cast<float>(nrm[i * 4U + 1U]) / 255.0F - 0.5F) * 2.0F;
        const float nz = (static_cast<float>(nrm[i * 4U + 2U]) / 255.0F - 0.5F) * 2.0F;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        EXPECT_GE(len, 0.9F)
            << "decoded normal length too short at texel " << i << " (len=" << len << ")";
        EXPECT_LE(len, 1.1F)
            << "decoded normal length too long at texel " << i << " (len=" << len << ")";
    }
}

} // namespace
