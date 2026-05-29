#include <cd/post/dof/Dof.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::post::dof::CameraSettings;
using cd::post::dof::coc_pixels;

constexpr float kEps = 0.05F;

TEST(PostDof, CocZeroAtFocusDistance)
{
    CameraSettings s {};
    s.focus_distance_m = 5.0F;
    EXPECT_LT(std::abs(coc_pixels(5.0F, s)), 0.5F);
}

TEST(PostDof, CocGrowsAwayFromFocus)
{
    CameraSettings s {};
    s.focus_distance_m = 5.0F;
    const float c_focus = coc_pixels(5.0F,   s);
    const float c_near  = coc_pixels(1.0F,   s);
    const float c_far   = coc_pixels(100.0F, s);
    EXPECT_GT(c_near, c_focus);
    EXPECT_GT(c_far,  c_focus);
}

TEST(PostDof, CocClampsToMax)
{
    CameraSettings s {};
    s.focus_distance_m = 5.0F;
    s.aperture_f_stop = 0.5F;  // very wide -> huge CoC
    s.max_coc_px = 30.0F;
    EXPECT_LE(coc_pixels(0.5F, s), 30.0F + kEps);
}

TEST(PostDof, CocZeroForZeroDepth)
{
    CameraSettings s {};
    EXPECT_NEAR(coc_pixels(0.0F, s), 0.0F, kEps);
}

TEST(PostDof, GlslKernelsNonEmpty)
{
    EXPECT_FALSE(cd::post::dof::kCocCS.empty());
    EXPECT_FALSE(cd::post::dof::kHexBlurCS.empty());
}

}  // namespace
