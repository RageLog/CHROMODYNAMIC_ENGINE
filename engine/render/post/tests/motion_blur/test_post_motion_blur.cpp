#include <cd/post/motion_blur/MotionBlur.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace
{

using cd::post::motion_blur::reduce_tile_max;
using cd::post::motion_blur::Settings;

constexpr float kEps = 1e-3F;

TEST(MotionBlur, TileMaxZeroForZeroVelocity)
{
    std::vector<cd::math::Vec2f> v(16 * 16, { 0, 0 });
    const auto m = reduce_tile_max(v, 16, 16, 0, 0, 16, 100.0F);
    EXPECT_NEAR(m.x, 0.0F, kEps);
    EXPECT_NEAR(m.y, 0.0F, kEps);
}

TEST(MotionBlur, TileMaxPicksLargestMagnitude)
{
    std::vector<cd::math::Vec2f> v(16 * 16, { 1, 0 });
    v[10 * 16 + 10] = { 5, 5 };  // magnitude ~7.07
    const auto m = reduce_tile_max(v, 16, 16, 0, 0, 16, 100.0F);
    EXPECT_NEAR(m.x, 5.0F, kEps);
    EXPECT_NEAR(m.y, 5.0F, kEps);
}

TEST(MotionBlur, TileMaxClampsToMaxMotion)
{
    std::vector<cd::math::Vec2f> v(16 * 16, { 100, 0 });
    const auto m = reduce_tile_max(v, 16, 16, 0, 0, 16, /*max_motion_px=*/40.0F);
    EXPECT_NEAR(std::sqrt(m.x * m.x + m.y * m.y), 40.0F, kEps);
}

TEST(MotionBlur, GlslKernelsNonEmpty)
{
    EXPECT_FALSE(cd::post::motion_blur::kVelocityTileMaxCS.empty());
    EXPECT_FALSE(cd::post::motion_blur::kMotionBlurGatherCS.empty());
}

}  // namespace
