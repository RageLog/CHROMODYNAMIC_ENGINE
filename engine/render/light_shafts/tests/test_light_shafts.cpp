#include <cd/light_shafts/LightShafts.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::light_shafts::Settings;
using cd::light_shafts::sun_screen_pos;

constexpr float kEps = 0.05F;

TEST(LightShafts, SunBehindCameraReturnsNegativeUv)
{
    // Camera looking +Z; sun direction pointing same way (sun behind).
    const auto uv = sun_screen_pos({ 0, 0,  1 },
                                   { 0, 0,  1 },
                                   { 1, 0,  0 },
                                   { 0, 1,  0 },
                                   1.0F);
    EXPECT_LT(uv.x, 0.0F);
}

TEST(LightShafts, SunOppositeCameraProjectsToCentre)
{
    // Camera looking +Z; sun direction is -Z (sun directly in front).
    const auto uv = sun_screen_pos({ 0, 0, -1 },
                                   { 0, 0,  1 },
                                   { 1, 0,  0 },
                                   { 0, 1,  0 },
                                   1.0F);
    EXPECT_NEAR(uv.x, 0.5F, kEps);
    EXPECT_NEAR(uv.y, 0.5F, kEps);
}

TEST(LightShafts, GlslKernelNonEmpty)
{
    EXPECT_FALSE(cd::light_shafts::kRadialBlurCS.empty());
}

}  // namespace
