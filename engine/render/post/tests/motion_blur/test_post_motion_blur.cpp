#include <cd/post/motion_blur/MotionBlur.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string_view>
#include <vector>

namespace
{

using cd::post::motion_blur::reduce_tile_max;
using cd::post::motion_blur::Settings;

constexpr float kEps = 1e-3F;

TEST(MotionBlur, TileMaxZeroForZeroVelocity)
{
    std::vector<cd::math::Vec2f> v(static_cast<std::size_t>(16) * 16, { 0, 0 });
    const auto m = reduce_tile_max(v, 16, 16, 0, 0, 16, 100.0F);
    EXPECT_NEAR(m.x, 0.0F, kEps);
    EXPECT_NEAR(m.y, 0.0F, kEps);
}

TEST(MotionBlur, TileMaxPicksLargestMagnitude)
{
    std::vector<cd::math::Vec2f> v(static_cast<std::size_t>(16) * 16, { 1, 0 });
    v[10 * 16 + 10] = { 5, 5 };  // magnitude ~7.07
    const auto m = reduce_tile_max(v, 16, 16, 0, 0, 16, 100.0F);
    EXPECT_NEAR(m.x, 5.0F, kEps);
    EXPECT_NEAR(m.y, 5.0F, kEps);
}

TEST(MotionBlur, TileMaxClampsToMaxMotion)
{
    std::vector<cd::math::Vec2f> v(static_cast<std::size_t>(16) * 16, { 100, 0 });
    const auto m = reduce_tile_max(v, 16, 16, 0, 0, 16, /*max_motion_px=*/40.0F);
    EXPECT_NEAR(std::sqrt(m.x * m.x + m.y * m.y), 40.0F, kEps);
}

TEST(MotionBlur, GlslKernelsNonEmpty)
{
    EXPECT_FALSE(cd::post::motion_blur::kVelocityTileMaxCS.empty());
    EXPECT_FALSE(cd::post::motion_blur::kMotionBlurGatherCS.empty());
}

// =============================================================================
// Edge / negative coverage (≥80→100).
// =============================================================================

TEST(MotionBlur, DefaultSettingsAreMcGuire)
{
    const Settings s {};
    EXPECT_EQ(s.tile_size, 16U);
    EXPECT_EQ(s.tap_count, 16U);
    EXPECT_FLOAT_EQ(s.max_motion_px, 40.0F);
}

TEST(MotionBlur, TileMaxSkipsOutOfBoundsPixelsOnEdgeTile)
{
    // 20x20 image, 16px tiles: tile (1,1) overhangs the image so most of
    // its 16x16 footprint is out of bounds. The reduce must skip the
    // overhang (no OOB read) and only consider the valid 4x4 corner.
    const std::uint32_t w = 20U;
    const std::uint32_t h = 20U;
    std::vector<cd::math::Vec2f> v(static_cast<std::size_t>(w) * h, { 0, 0 });
    // Put a strong velocity inside the valid corner of tile (1,1) at (18,18).
    v[18U * w + 18U] = { 7, 0 };
    const auto m = reduce_tile_max(v, w, h, /*tile_x=*/1U, /*tile_y=*/1U,
                                   /*tile_size=*/16U, /*max_motion_px=*/100.0F);
    EXPECT_NEAR(m.x, 7.0F, kEps);
    EXPECT_NEAR(m.y, 0.0F, kEps);
}

TEST(MotionBlur, TileMaxFullyOutOfBoundsTileReturnsZero)
{
    // tile (5,5) starts at pixel (80,80) — entirely past a 32x32 image.
    const std::uint32_t w = 32U;
    const std::uint32_t h = 32U;
    std::vector<cd::math::Vec2f> v(static_cast<std::size_t>(w) * h, { 9, 9 });
    const auto m = reduce_tile_max(v, w, h, 5U, 5U, 16U, 100.0F);
    EXPECT_NEAR(m.x, 0.0F, kEps);
    EXPECT_NEAR(m.y, 0.0F, kEps);
}

TEST(MotionBlur, TileMaxClampPreservesDirection)
{
    // A diagonal velocity over the cap must clamp magnitude to max_motion_px
    // while keeping the unit direction (not snapping to an axis).
    std::vector<cd::math::Vec2f> v(static_cast<std::size_t>(16) * 16, { 0, 0 });
    v[0] = { 30.0F, 40.0F };  // magnitude 50, direction (0.6, 0.8)
    const auto m = reduce_tile_max(v, 16, 16, 0, 0, 16, /*max_motion_px=*/25.0F);
    const float mag = std::sqrt(m.x * m.x + m.y * m.y);
    EXPECT_NEAR(mag, 25.0F, kEps);
    EXPECT_NEAR(m.x / mag, 0.6F, kEps);
    EXPECT_NEAR(m.y / mag, 0.8F, kEps);
}

TEST(MotionBlur, TileMaxUnderCapIsUnchanged)
{
    // Magnitude below the cap passes through untouched (no spurious scaling).
    std::vector<cd::math::Vec2f> v(static_cast<std::size_t>(16) * 16, { 0, 0 });
    v[0] = { 3.0F, 4.0F };  // magnitude 5
    const auto m = reduce_tile_max(v, 16, 16, 0, 0, 16, /*max_motion_px=*/40.0F);
    EXPECT_NEAR(m.x, 3.0F, kEps);
    EXPECT_NEAR(m.y, 4.0F, kEps);
}

TEST(MotionBlur, GatherKernelDeclaresTileMaxAndVelocityTaps)
{
    const std::string_view cs { cd::post::motion_blur::kMotionBlurGatherCS };
    EXPECT_NE(cs.find("tile_max"), std::string_view::npos);
    EXPECT_NE(cs.find("velocity"), std::string_view::npos);
    EXPECT_NE(cs.find("tap_count"), std::string_view::npos);
}

}  // namespace
