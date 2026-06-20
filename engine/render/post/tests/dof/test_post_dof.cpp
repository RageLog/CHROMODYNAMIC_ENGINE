#include <cd/post/dof/Dof.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <string_view>

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

// =============================================================================
// Edge / negative coverage (≥80→100).
// =============================================================================

TEST(PostDof, DefaultCameraSettingsAreFilmStandard)
{
    // Lock the documented thin-lens defaults so a refactor can't drift the
    // CoC baseline (which downstream max-blur radius scales off).
    const CameraSettings s {};
    EXPECT_FLOAT_EQ(s.focal_length_mm, 35.0F);
    EXPECT_FLOAT_EQ(s.aperture_f_stop, 2.8F);
    EXPECT_FLOAT_EQ(s.focus_distance_m, 5.0F);
    EXPECT_FLOAT_EQ(s.sensor_height_mm, 24.0F);
    EXPECT_FLOAT_EQ(s.resolution_h_px, 1080.0F);
    EXPECT_FLOAT_EQ(s.max_coc_px, 30.0F);
}

TEST(PostDof, CocNegativeDepthReturnsZero)
{
    // depth <= 0 is non-physical; the guard must hand back 0, never NaN.
    CameraSettings s {};
    EXPECT_NEAR(coc_pixels(-3.0F, s), 0.0F, kEps);
    EXPECT_TRUE(std::isfinite(coc_pixels(-3.0F, s)));
}

TEST(PostDof, CocIsNonNegativeAcrossDepthSweep)
{
    // coc_pixels abs()-wraps the thin-lens signed CoC, so blur magnitude is
    // never negative regardless of in-front / behind focus.
    CameraSettings s {};
    s.focus_distance_m = 5.0F;
    for (int di = 0; di <= 99; ++di)
    {
        const float d = 0.1F + 0.5F * static_cast<float>(di);
        EXPECT_GE(coc_pixels(d, s), 0.0F) << "depth=" << d;
    }
}

TEST(PostDof, CocNeverExceedsMaxEvenAtExtremeAperture)
{
    // f/0.7 is past any real lens; the min(coc, max_coc_px) clamp must hold.
    CameraSettings s {};
    s.focus_distance_m = 5.0F;
    s.aperture_f_stop = 0.7F;
    s.max_coc_px = 12.0F;
    for (int di = 0; di <= 99; ++di)
    {
        const float d = 0.1F + 1.0F * static_cast<float>(di);
        EXPECT_LE(coc_pixels(d, s), 12.0F + kEps) << "depth=" << d;
    }
}

TEST(PostDof, CocScalesWithResolution)
{
    // CoC in pixels is proportional to vertical resolution (sensor->pixel
    // mapping). Doubling resolution roughly doubles the pixel CoC.
    CameraSettings lo {};
    lo.focus_distance_m = 5.0F;
    lo.resolution_h_px = 540.0F;
    lo.max_coc_px = 1.0e6F;  // lift the clamp so the ratio is visible
    CameraSettings hi = lo;
    hi.resolution_h_px = 1080.0F;
    const float c_lo = coc_pixels(1.0F, lo);
    const float c_hi = coc_pixels(1.0F, hi);
    ASSERT_GT(c_lo, kEps);
    EXPECT_NEAR(c_hi / c_lo, 2.0F, 0.05F);
}

TEST(PostDof, CocKernelDeclaresThinLensInputs)
{
    // The CoC compute kernel must carry the aperture + focus push-constants
    // (the host coc_pixels and the GLSL must stay in lockstep).
    const std::string_view cs { cd::post::dof::kCocCS };
    EXPECT_NE(cs.find("aperture_f_stop"), std::string_view::npos);
    EXPECT_NE(cs.find("focus_distance_m"), std::string_view::npos);
    EXPECT_NE(cs.find("max_coc_px"), std::string_view::npos);
}

}  // namespace
