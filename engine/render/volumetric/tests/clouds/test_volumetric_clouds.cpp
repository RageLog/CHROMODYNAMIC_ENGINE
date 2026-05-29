#include <cd/volumetric/clouds/Clouds.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::volumetric::clouds::density;
using cd::volumetric::clouds::height_fraction;
using cd::volumetric::clouds::remap;
using cd::volumetric::clouds::Settings;

constexpr float kEps = 1e-3F;

TEST(Clouds, HeightFractionZeroOutsideLayer)
{
    Settings s {};
    EXPECT_NEAR(height_fraction(0.5F, s), 0.0F, kEps);
    EXPECT_NEAR(height_fraction(6.0F, s), 0.0F, kEps);
}

TEST(Clouds, HeightFractionPeaksInMiddle)
{
    Settings s {};
    const float mid = (s.layer_bottom_km + s.layer_top_km) * 0.5F;
    EXPECT_NEAR(height_fraction(mid, s), 1.0F, kEps);
}

TEST(Clouds, DensityZeroBelowCoverage)
{
    Settings s {};
    s.coverage = 0.3F;
    EXPECT_NEAR(density(3.0F, 0.5F, s), 0.0F, kEps);  // 0.5 < (1 - 0.3) = 0.7
}

TEST(Clouds, DensityPositiveAboveCoverage)
{
    Settings s {};
    s.coverage = 0.7F;
    EXPECT_GT(density(3.0F, 0.6F, s), 0.0F);
}

TEST(Clouds, RemapMapsRanges)
{
    EXPECT_NEAR(remap(5.0F, 0.0F, 10.0F, 0.0F, 1.0F), 0.5F, kEps);
    EXPECT_NEAR(remap(15.0F, 0.0F, 10.0F, 0.0F, 1.0F), 1.0F, kEps);
}

TEST(Clouds, GlslKernelNonEmpty)
{
    EXPECT_FALSE(cd::volumetric::clouds::kCloudsMarchCS.empty());
}

}  // namespace
