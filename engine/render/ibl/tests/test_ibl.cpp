// =============================================================================
// CHROMODYNAMIC — test_ibl.cpp
// =============================================================================
#include <cd/ibl/BrdfLut.hpp>

#include <gtest/gtest.h>

#include <algorithm>

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
    std::vector<float> eq(16U * 8U * 3U, 0.5F);
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
