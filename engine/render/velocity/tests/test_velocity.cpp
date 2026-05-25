#include <cd/velocity/Velocity.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::velocity::motion_pixels;
using cd::velocity::motion_vector_uv;

constexpr float kEps = 1e-3F;

TEST(Velocity, MotionZeroForIdenticalClipPositions)
{
    const cd::math::Vec4f p { 0.4F, -0.2F, 0.5F, 1.0F };
    const auto mv = motion_vector_uv(p, p);
    EXPECT_NEAR(mv.x, 0.0F, kEps);
    EXPECT_NEAR(mv.y, 0.0F, kEps);
}

TEST(Velocity, MotionVectorMatchesUvDelta)
{
    // Prev clip projects to (0, 0); curr clip projects to (0.25, 0).
    const cd::math::Vec4f prev { 0.0F, 0.0F, 0.5F, 1.0F };  // uv = (0.5, 0.5)
    const cd::math::Vec4f curr { 0.5F, 0.0F, 0.5F, 1.0F };  // uv = (0.75, 0.5)
    const auto mv = motion_vector_uv(prev, curr);
    EXPECT_NEAR(mv.x, 0.25F, kEps);
    EXPECT_NEAR(mv.y, 0.0F,  kEps);
}

TEST(Velocity, MotionZeroWhenEitherBehindNearPlane)
{
    const cd::math::Vec4f behind { 0, 0, 0, -1 };
    const cd::math::Vec4f front  { 0, 0, 0.5F, 1 };
    EXPECT_NEAR(motion_vector_uv(behind, front).x, 0.0F, kEps);
    EXPECT_NEAR(motion_vector_uv(front, behind).x, 0.0F, kEps);
}

TEST(Velocity, MotionPixelsScalesWithExtent)
{
    const cd::math::Vec2f uv_delta { 0.1F, 0.0F };
    EXPECT_NEAR(motion_pixels(uv_delta, 1920, 1080),  192.0F, 0.5F);
    EXPECT_NEAR(motion_pixels(uv_delta, 3840, 2160),  384.0F, 0.5F);
}

TEST(Velocity, GlslSourcesNonEmpty)
{
    EXPECT_FALSE(cd::velocity::kVelocityVS.empty());
    EXPECT_FALSE(cd::velocity::kVelocityFS.empty());
    EXPECT_NE(cd::velocity::kVelocityFS.find("out_motion"),
              std::string_view::npos);
}

}  // namespace
