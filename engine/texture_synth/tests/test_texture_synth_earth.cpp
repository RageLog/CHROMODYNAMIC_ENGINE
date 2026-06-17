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
//   E1 albedo: size*size*4 bytes, alpha channel all 255
//   E2 albedo: deterministic — two bakes of the same size are byte-identical
//   E3 normal: size*size*4 bytes, alpha 255, z-channel (B) biased toward +1
//   E4 normal: deterministic
//   E5 mr: size*size*4 bytes, alpha 255, R channel unused (== 0) per glTF MR
//   E6 mr: deterministic
//   E7 all three bakers tolerate the smallest sensible size (1×1) without UB
// =============================================================================
#include <gtest/gtest.h>

#include <cd/texture_synth/Earth.hpp>

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

} // namespace
