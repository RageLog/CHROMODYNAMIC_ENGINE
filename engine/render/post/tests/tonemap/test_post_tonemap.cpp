// =============================================================================
// cd::post_tonemap unit tests — Day 13.
// =============================================================================
#include <cd/post/tonemap/Tonemap.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::post::tonemap::agx;
using cd::post::tonemap::hable;
using cd::post::tonemap::hill_aces;
using cd::post::tonemap::narkowicz;
using cd::post::tonemap::Operator;
using cd::post::tonemap::tonemap;

constexpr float kEps = 1e-3F;

TEST(PostTonemap, BlackInIsBlackOutAllOperators)
{
    const cd::math::Vec3f k { 0.0F, 0.0F, 0.0F };
    for (auto op : { Operator::kNarkowicz, Operator::kHill,
                     Operator::kHable, Operator::kAgx })
    {
        const auto o = tonemap(k, op);
        // Hill / Narkowicz / Hable hit exact zero; AGX may have a
        // tiny negative offset from the polynomial that the clamp
        // pulls back to 0 — both acceptable.
        EXPECT_NEAR(o.x, 0.0F, kEps) << static_cast<int>(op);
        EXPECT_NEAR(o.y, 0.0F, kEps) << static_cast<int>(op);
        EXPECT_NEAR(o.z, 0.0F, kEps) << static_cast<int>(op);
    }
}

TEST(PostTonemap, OutputClampedToOneAllOperators)
{
    // Far brighter than display white (10×) — every operator must hand
    // back something inside [0, 1] thanks to the inline clamp.
    const cd::math::Vec3f bright { 10.0F, 10.0F, 10.0F };
    for (auto op : { Operator::kNarkowicz, Operator::kHill,
                     Operator::kHable, Operator::kAgx })
    {
        const auto o = tonemap(bright, op);
        EXPECT_LE(o.x, 1.0F + kEps);
        EXPECT_LE(o.y, 1.0F + kEps);
        EXPECT_LE(o.z, 1.0F + kEps);
        EXPECT_GE(o.x, 0.0F - kEps);
    }
}

TEST(PostTonemap, MonotonicallyIncreasing)
{
    // Larger linear input -> larger tonemapped output (per-channel).
    // True for all four operators across the 0-mid range.
    for (auto op : { Operator::kNarkowicz, Operator::kHill,
                     Operator::kHable, Operator::kAgx })
    {
        float prev = -1.0F;
        for (float v = 0.0F; v <= 2.0F; v += 0.05F)
        {
            const auto out = tonemap({ v, v, v }, op);
            EXPECT_GE(out.x + kEps, prev) << "op=" << static_cast<int>(op)
                                          << " v=" << v;
            prev = out.x;
        }
    }
}

TEST(PostTonemap, NarkowiczAnchorValuesMatchPublishedFit)
{
    // Anchor at unit input: Narkowicz's curve evaluates to ~0.798 for
    // R=G=B=1.0 (verified against Filament's reference test).
    const auto out = narkowicz({ 1.0F, 1.0F, 1.0F });
    EXPECT_NEAR(out.x, 0.797F, 0.01F);
    EXPECT_NEAR(out.y, 0.797F, 0.01F);
    EXPECT_NEAR(out.z, 0.797F, 0.01F);
}

TEST(PostTonemap, AgxKeepsSaturationOnSingleChannelHighlight)
{
    // Pure-red HDR pixel (1, 0, 0) should stay redder under AGX than
    // under Hill — that's literally why AGX exists. We don't need an
    // exact target; just verify R > G+B by a meaningful margin under
    // AGX, and that the gap is at least as wide as Hill's gap.
    const cd::math::Vec3f red { 4.0F, 0.0F, 0.0F };
    const auto a = agx(red);
    const auto h = hill_aces(red);
    const float a_gap = a.x - (a.y + a.z);
    const float h_gap = h.x - (h.y + h.z);
    EXPECT_GT(a_gap, 0.4F);
    EXPECT_GE(a_gap + kEps, h_gap);
}

TEST(PostTonemap, GlslDispatcherReturnsNonEmptyForEveryOperator)
{
    for (auto op : { Operator::kNarkowicz, Operator::kHill,
                     Operator::kHable, Operator::kAgx })
    {
        const auto src = cd::post::tonemap::glsl_for(op);
        EXPECT_FALSE(src.empty());
        // Sanity: every operator's GLSL must define the cd_tonemap entry.
        EXPECT_NE(src.find("cd_tonemap"), std::string_view::npos)
            << "op=" << static_cast<int>(op);
    }
}

}  // namespace
