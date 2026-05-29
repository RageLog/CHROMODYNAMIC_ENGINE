#include <cd/brdf/sheen_clearcoat/SheenClearcoat.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::brdf::sheen_clearcoat::charlie_d;
using cd::brdf::sheen_clearcoat::clearcoat_d_v;
using cd::brdf::sheen_clearcoat::v_neubelt;

TEST(SheenClearcoat, CharlieDPositiveAtGrazing)
{
    // Charlie peaks at sin^2 ~ 1; n_dot_h ~ 0 means full grazing.
    EXPECT_GT(charlie_d(0.3F, 0.0F), 0.0F);
    EXPECT_GT(charlie_d(0.5F, 0.3F), 0.0F);
}

TEST(SheenClearcoat, CharlieDPeaksAtGrazing)
{
    // Sheen distribution peaks at sin^2 = 1 (grazing-angle scatter).
    EXPECT_GT(charlie_d(0.3F, 0.0F), charlie_d(0.3F, 1.0F));
}

TEST(SheenClearcoat, NeubeltVisibilityFiniteAndPositive)
{
    EXPECT_GT(v_neubelt(0.5F, 0.5F), 0.0F);
    EXPECT_GT(v_neubelt(0.1F, 0.1F), 0.0F);
}

TEST(SheenClearcoat, ClearcoatDvFiniteAtNormalIncidence)
{
    const float dv = clearcoat_d_v(0.2F, 1.0F, 1.0F, 1.0F);
    EXPECT_GT(dv, 0.0F);
    EXPECT_TRUE(std::isfinite(dv));
}

TEST(SheenClearcoat, GlslNonEmpty)
{
    EXPECT_FALSE(cd::brdf::sheen_clearcoat::kSheenClearcoatGlsl.empty());
}

}  // namespace
