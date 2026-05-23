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
