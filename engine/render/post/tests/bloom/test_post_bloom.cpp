// =============================================================================
// cd::post_bloom unit tests — Day 8.
// =============================================================================
#include <cd/post/bloom/Bloom.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::post::bloom::CpuImage;
using cd::post::bloom::downsample_box;
using cd::post::bloom::Settings;
using cd::post::bloom::soft_threshold;
using cd::post::bloom::upsample_tent;

constexpr float kEps = 1e-3F;

TEST(PostBloom, SoftThresholdBelowKneeEmitsZero)
{
    Settings s {};
    s.threshold = 1.0F;
    s.knee = 0.5F;
    // 0.4 is well under the knee (1.0 - 0.5 = 0.5).
    const cd::math::Vec3f c { 0.4F, 0.4F, 0.4F };
    const auto out = soft_threshold(c, s);
    EXPECT_NEAR(out.x, 0.0F, kEps);
    EXPECT_NEAR(out.y, 0.0F, kEps);
    EXPECT_NEAR(out.z, 0.0F, kEps);
}

TEST(PostBloom, SoftThresholdWellAboveLetsThroughExcess)
{
    Settings s {};
    s.threshold = 1.0F;
    s.knee = 0.5F;
    // 3.0 input, threshold 1.0: pixel is 3x as bright as threshold;
    // soft-knee math hands back something proportional to (br - thr).
    const cd::math::Vec3f c { 3.0F, 3.0F, 3.0F };
    const auto out = soft_threshold(c, s);
    EXPECT_GT(out.x, 1.5F);    // most of the pixel passes
    EXPECT_LT(out.x, 3.0F);
}

TEST(PostBloom, DownsampleBoxHalvesDimensions)
{
    CpuImage img { 8, 8, std::vector<cd::math::Vec3f>(64, { 1, 1, 1 }) };
    const auto d = downsample_box(img);
    EXPECT_EQ(d.w, 4U);
    EXPECT_EQ(d.h, 4U);
    // Uniform input -> uniform output, value preserved.
    EXPECT_NEAR(d.at(0, 0).x, 1.0F, kEps);
    EXPECT_NEAR(d.at(3, 3).x, 1.0F, kEps);
}

TEST(PostBloom, DownsampleBoxAveragesPixels)
{
    // 2x2 checkerboard at full res; box-halve should produce uniform 0.5.
    CpuImage img { 2, 2, {} };
    img.pixels = { {1, 0, 0}, {0, 0, 0}, {0, 0, 0}, {1, 0, 0} };
    const auto d = downsample_box(img);
    EXPECT_EQ(d.w, 1U);
    EXPECT_EQ(d.h, 1U);
    EXPECT_NEAR(d.at(0, 0).x, 0.5F, kEps);
}

TEST(PostBloom, UpsampleTentDoublesDimensions)
{
    CpuImage img { 2, 2, {} };
    img.pixels = { {1,1,1}, {1,1,1}, {1,1,1}, {1,1,1} };
    const auto u = upsample_tent(img, 4, 4);
    EXPECT_EQ(u.w, 4U);
    EXPECT_EQ(u.h, 4U);
    EXPECT_NEAR(u.at(2, 2).x, 1.0F, kEps);  // uniform interior
}

TEST(PostBloom, GlslKernelsNonEmpty)
{
    EXPECT_FALSE(cd::post::bloom::kDownsampleCS.empty());
    EXPECT_FALSE(cd::post::bloom::kUpsampleCS.empty());
    EXPECT_FALSE(cd::post::bloom::kPrefilterCS.empty());
    // Each kernel must declare the local workgroup size (sanity).
    EXPECT_NE(cd::post::bloom::kDownsampleCS.find("local_size_x"),
              std::string_view::npos);
    EXPECT_NE(cd::post::bloom::kUpsampleCS.find("local_size_x"),
              std::string_view::npos);
}

}  // namespace
