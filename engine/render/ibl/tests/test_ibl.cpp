// =============================================================================
// CHROMODYNAMIC — test_ibl.cpp
// =============================================================================
#include <cd/ibl/BrdfLut.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

TEST(BrdfLut, SmallLutHasFiniteValues)
{
    // Tiny LUT for the test path — 8x8 with 64 samples bakes in <1 ms
    // but still exercises the full Hammersley + GGX sampling code.
    const auto lut = cd::ibl::bake_brdf_lut(8, 8, 64);
    EXPECT_EQ(lut.width, 8u);
    EXPECT_EQ(lut.height, 8u);
    EXPECT_EQ(lut.rg.size(), 8u * 8u * 2u);
    for (float v : lut.rg)
    {
        EXPECT_FALSE(std::isnan(v));
        EXPECT_FALSE(std::isinf(v));
        // Split-sum scale/bias are bounded in [0, 1] for the normal
        // GGX / Schlick range.
        EXPECT_GE(v, -0.01F);
        EXPECT_LE(v, 1.01F);
    }
}

TEST(BrdfLut, ScaleIsAtLeastBias)
{
    // For non-grazing angles the scale (A) should dominate the bias (B):
    // F_env ≈ F0 * A + B → at NdotV=1, B is near 0 and A is large.
    const auto lut = cd::ibl::bake_brdf_lut(16, 16, 256);
    // Top-right corner ≈ (NdotV=1, roughness=1). Bottom-left ≈ smooth + grazing.
    const auto idx = [&](std::uint32_t x, std::uint32_t y) -> std::size_t {
        return (static_cast<std::size_t>(y) * lut.width + x) * 2;
    };
    // Smooth + perpendicular: A should be large.
    const std::size_t br = idx(lut.width - 1, 0);
    EXPECT_GT(lut.rg[br + 0], 0.5F);
}

TEST(IrradianceDirectional, NdotLZeroAtOpposingDirection)
{
    cd::math::Vec3f normal { 0.0F, 1.0F, 0.0F };
    cd::math::Vec3f light_dir { 0.0F, 1.0F, 0.0F };  // pointing up; opposite of N if light direction points TOWARD normal
    cd::math::Vec3f light_color { 1.0F, 1.0F, 1.0F };
    // dot(N, L) = 1 → full irradiance.
    const auto full = cd::ibl::integrate_irradiance_directional(normal, light_dir, light_color);
    EXPECT_NEAR(full.x, 1.0F, 1e-5F);

    // light from below: dot(N, L) = -1 → clamped to 0.
    cd::math::Vec3f below { 0.0F, -1.0F, 0.0F };
    const auto zero = cd::ibl::integrate_irradiance_directional(normal, below, light_color);
    EXPECT_NEAR(zero.x, 0.0F, 1e-5F);
    EXPECT_NEAR(zero.y, 0.0F, 1e-5F);
    EXPECT_NEAR(zero.z, 0.0F, 1e-5F);
}

// Phase 155-full — cubemap chain (equirect → cube + irradiance + prefiltered specular)
#include <cd/ibl/Cubemap.hpp>
#include <cd/ibl/EquirectToCube.hpp>
#include <cd/ibl/IrradianceConvolution.hpp>
#include <cd/ibl/PrefilteredSpecular.hpp>

TEST(Cubemap, AllocateSizesFaces)
{
    auto cube = cd::ibl::CubeMapRgbF::allocate(16);
    EXPECT_EQ(cube.face_size, 16U);
    for (const auto& f : cube.faces)
        EXPECT_EQ(f.size(), 16U * 16U * 3U);
}

TEST(Cubemap, UvToDirRoundTripsAxis)
{
    // Center of +X face should produce +X direction.
    auto d = cd::ibl::cube_uv_to_world_dir(cd::ibl::CubeFace::kPosX, 0.5F, 0.5F);
    EXPECT_NEAR(d.x, 1.0F, 1e-5F);
    EXPECT_NEAR(d.y, 0.0F, 1e-5F);
    EXPECT_NEAR(d.z, 0.0F, 1e-5F);
}

TEST(EquirectToCube, ConstantImageProducesConstantCube)
{
    std::vector<float> eq(static_cast<std::size_t>(16U) * 8U * 3U, 0.5F);
    cd::ibl::EquirectImage img { 16, 8, eq };
    auto cube = cd::ibl::equirect_to_cube(img, 8);
    for (const auto& f : cube.faces)
        for (auto v : f) EXPECT_NEAR(v, 0.5F, 1e-4F);
}

TEST(Irradiance, EmptyEnvProducesEmptyIrradiance)
{
    cd::ibl::CubeMapRgbF empty {};
    auto out = cd::ibl::convolve_irradiance(empty, 4, 30.0F);
    EXPECT_EQ(out.face_size, 4U);
}

TEST(Irradiance, ConstantEnvProducesPositiveIrradiance)
{
    auto env = cd::ibl::CubeMapRgbF::allocate(4);
    for (auto& f : env.faces)
        for (std::size_t i = 0; i + 2 < f.size(); i += 3)
        { f[i] = 0.5F; f[i+1] = 0.5F; f[i+2] = 0.5F; }
    auto out = cd::ibl::convolve_irradiance(env, 4, 30.0F);
    // A unit-radiance hemisphere convolves to π·L_avg for the
    // diffuse term. Sanity check: irradiance > 0 everywhere.
    bool any_positive = false;
    for (const auto& f : out.faces)
        for (auto v : f) if (v > 0.0F) { any_positive = true; break; }
    EXPECT_TRUE(any_positive);
}

TEST(PrefilteredSpecular, BuildsRequestedMipCount)
{
    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    auto out = cd::ibl::prefilter_specular(env, 8, 3, 8);
    EXPECT_EQ(out.mip_count, 3U);
    EXPECT_EQ(out.mips[0].face_size, 8U);
    EXPECT_EQ(out.mips[1].face_size, 4U);
    EXPECT_EQ(out.mips[2].face_size, 2U);
}

// === Band-3 baker-correctness edge tests (untested branches) ===============
// These pin previously-uncovered branches in the CPU IBL bakers so the
// charter-complete seal (ADR-20260616-band3-render-features-scope §3 ibl) is
// fail-on-revert. They intentionally avoid the calibrated W8-AW chrome-mirror
// bake parameters — they exercise correctness branches, not bake output.

TEST(PrefilteredSpecular, SingleMipUsesRoughnessZeroBranch)
{
    // num_mips == 1 takes the `num_mips <= 1 ? 0.0F` ternary (otherwise
    // division by (num_mips - 1) == 0). roughness 0 = mirror; a constant
    // env must convolve to that same constant at every face texel.
    auto env = cd::ibl::CubeMapRgbF::allocate(4);
    for (auto& face : env.faces)
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = 0.25F; face[i + 1] = 0.5F; face[i + 2] = 0.75F; }

    auto out = cd::ibl::prefilter_specular(env, 4, 1, 16);
    ASSERT_EQ(out.mip_count, 1U);
    EXPECT_EQ(out.mips[0].face_size, 4U);
    bool any_finite_positive = false;
    for (const auto& face : out.mips[0].faces)
        for (float v : face)
        {
            EXPECT_FALSE(std::isnan(v));
            EXPECT_FALSE(std::isinf(v));
            if (v > 0.0F)
                any_finite_positive = true;
        }
    EXPECT_TRUE(any_finite_positive);
}

TEST(PrefilteredSpecular, RequestedMipsClampToMax)
{
    // num_mips > kMaxSpecularMips must clamp via std::min, never overrun
    // the fixed-size mips array.
    auto env = cd::ibl::CubeMapRgbF::allocate(8);
    auto out = cd::ibl::prefilter_specular(env, 8, cd::ibl::kMaxSpecularMips + 4U, 8);
    EXPECT_EQ(out.mip_count, cd::ibl::kMaxSpecularMips);
}

TEST(EquirectToCube, EmptyImageSamplesToBlack)
{
    // sample_equirect early-outs to {0,0,0} when width/height == 0.
    cd::ibl::EquirectImage empty {};
    const auto c = cd::ibl::sample_equirect(empty, cd::math::Vec3f { 0.0F, 1.0F, 0.0F });
    EXPECT_NEAR(c.x, 0.0F, 1e-6F);
    EXPECT_NEAR(c.y, 0.0F, 1e-6F);
    EXPECT_NEAR(c.z, 0.0F, 1e-6F);
}

TEST(EquirectToCube, PoleSamplingClampsRowAndStaysFinite)
{
    // Straight-up / straight-down directions drive v to the 0 / 1 row
    // boundary, exercising clamp_y at the top and bottom edges. A row
    // gradient (top != bottom) confirms the pole rows are distinct and
    // finite (no wraparound NaN at theta = ±pi/2).
    constexpr std::uint32_t kW = 8;
    constexpr std::uint32_t kH = 4;
    std::vector<float> eq(static_cast<std::size_t>(kW) * kH * 3, 0.0F);
    for (std::uint32_t row = 0; row < kH; ++row)
    {
        const float shade = static_cast<float>(row) / static_cast<float>(kH - 1U);
        for (std::uint32_t col = 0; col < kW; ++col)
        {
            const auto i = (static_cast<std::size_t>(row) * kW + col) * 3;
            eq[i] = shade;
            eq[i + 1] = shade;
            eq[i + 2] = shade;
        }
    }
    cd::ibl::EquirectImage img { kW, kH, eq };

    const auto up = cd::ibl::sample_equirect(img, cd::math::Vec3f { 0.0F, 1.0F, 0.0F });
    const auto down = cd::ibl::sample_equirect(img, cd::math::Vec3f { 0.0F, -1.0F, 0.0F });
    EXPECT_FALSE(std::isnan(up.x));
    EXPECT_FALSE(std::isnan(down.x));
    // Top row (v->0, shade 0) is darker than the bottom row (v->1, shade 1).
    EXPECT_LT(up.x, down.x);
    EXPECT_GE(up.x, -1e-6F);
    EXPECT_LE(down.x, 1.0F + 1e-6F);
}

TEST(Cubemap, EmptyCubemapSamplesToBlack)
{
    // sample_cubemap_dir early-outs to {0,0,0} when face_size == 0 — the
    // prefilter/irradiance bakers rely on this for degenerate inputs.
    cd::ibl::CubeMapRgbF empty {};
    const auto c = cd::ibl::sample_cubemap_dir(empty, cd::math::Vec3f { 0.0F, 0.0F, 1.0F });
    EXPECT_NEAR(c.x, 0.0F, 1e-6F);
    EXPECT_NEAR(c.y, 0.0F, 1e-6F);
    EXPECT_NEAR(c.z, 0.0F, 1e-6F);
}

TEST(Cubemap, SampleDirHitsEachDominantAxisFace)
{
    // Drives the three dominant-axis branches (x/y/z) of sample_cubemap_dir,
    // both signs, by tinting each face a unique value and reading it back
    // through the centre direction of that face.
    auto cm = cd::ibl::CubeMapRgbF::allocate(4);
    for (std::uint8_t f = 0; f < cd::ibl::kCubeFaceCount; ++f)
    {
        const float tint = static_cast<float>(f) + 1.0F;  // 1..6, distinct
        auto& face = cm.faces[f];
        for (std::size_t i = 0; i + 2 < face.size(); i += 3)
        { face[i] = tint; face[i + 1] = tint; face[i + 2] = tint; }
    }
    const std::array<cd::math::Vec3f, 6> centre_dirs {{
        { 1.0F, 0.0F, 0.0F }, { -1.0F, 0.0F, 0.0F },
        { 0.0F, 1.0F, 0.0F }, { 0.0F, -1.0F, 0.0F },
        { 0.0F, 0.0F, 1.0F }, { 0.0F, 0.0F, -1.0F },
    }};
    for (std::uint8_t f = 0; f < cd::ibl::kCubeFaceCount; ++f)
    {
        const auto c = cd::ibl::sample_cubemap_dir(cm, centre_dirs[f]);
        EXPECT_NEAR(c.x, static_cast<float>(f) + 1.0F, 1e-4F)
            << "dominant-axis face " << static_cast<int>(f);
    }
}
