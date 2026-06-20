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
        for (int vi = 0; vi <= 40; ++vi)
        {
            const float v = 0.05F * static_cast<float>(vi);
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

// =============================================================================
// Edge / contract coverage (≥80→100).
// =============================================================================

TEST(PostTonemap, OperatorEnumValuesAreStable)
{
    // The composite FS rounds pc.fx.x to these exact integer IDs; drift
    // would silently re-route the tonemap operator in the rendered frame.
    EXPECT_EQ(static_cast<int>(Operator::kNarkowicz), 0);
    EXPECT_EQ(static_cast<int>(Operator::kHill), 1);
    EXPECT_EQ(static_cast<int>(Operator::kHable), 2);
    EXPECT_EQ(static_cast<int>(Operator::kAgx), 3);
}

TEST(PostTonemap, DispatcherMatchesDirectCallForEachOperator)
{
    const cd::math::Vec3f c { 0.6F, 0.3F, 0.9F };
    const auto n = tonemap(c, Operator::kNarkowicz);
    const auto h = tonemap(c, Operator::kHill);
    const auto b = tonemap(c, Operator::kHable);
    const auto a = tonemap(c, Operator::kAgx);
    EXPECT_NEAR(n.x, narkowicz(c).x, kEps);
    EXPECT_NEAR(h.y, hill_aces(c).y, kEps);
    EXPECT_NEAR(b.z, hable(c).z, kEps);
    EXPECT_NEAR(a.x, agx(c).x, kEps);
}

TEST(PostTonemap, HableWhitePointMapsNearUnity)
{
    // Hable normalises by 1/curve(W=11.2); feeding the white point W back in
    // should land at ~1.0 (the curve's defined display-white anchor).
    const auto out = hable({ 11.2F, 11.2F, 11.2F });
    EXPECT_NEAR(out.x, 1.0F, 0.02F);
}

TEST(PostTonemap, HillAcesUnitInputIsBelowWhite)
{
    // Hill ACES at linear 1.0 sits below display white (the shoulder rolls
    // off); lock it to a sane mid-high value so a coefficient typo trips.
    const auto out = hill_aces({ 1.0F, 1.0F, 1.0F });
    EXPECT_GT(out.x, 0.6F);
    EXPECT_LT(out.x, 1.0F);
}

TEST(PostTonemap, AllOperatorsClampNegativeInputToZeroFloor)
{
    // Negative HDR (e.g. from an over-eager subtractive effect upstream)
    // must not produce negative LDR — the inline clamp pins the floor at 0.
    const cd::math::Vec3f neg { -1.0F, -0.5F, -0.25F };
    for (auto op : { Operator::kNarkowicz, Operator::kHill,
                     Operator::kHable, Operator::kAgx })
    {
        const auto o = tonemap(neg, op);
        EXPECT_GE(o.x, 0.0F - kEps) << "op=" << static_cast<int>(op);
        EXPECT_GE(o.y, 0.0F - kEps) << "op=" << static_cast<int>(op);
        EXPECT_GE(o.z, 0.0F - kEps) << "op=" << static_cast<int>(op);
    }
}

}  // namespace
