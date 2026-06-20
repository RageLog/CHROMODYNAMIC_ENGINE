// =============================================================================
// cd::post_taa unit tests — Day 11.
// =============================================================================
#include <cd/post/taa/Taa.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace
{

using cd::post::taa::clip_aabb;
using cd::post::taa::halton;
using cd::post::taa::jitter_offset;
using cd::post::taa::neighborhood_box;
using cd::post::taa::resolve;
using cd::post::taa::Settings;

constexpr float kEps = 1e-3F;

TEST(PostTaa, HaltonSequenceKnownValues)
{
    // Halton(1, 2) = 0.5; Halton(2, 2) = 0.25; Halton(3, 2) = 0.75.
    EXPECT_NEAR(halton(1, 2), 0.5F,  kEps);
    EXPECT_NEAR(halton(2, 2), 0.25F, kEps);
    EXPECT_NEAR(halton(3, 2), 0.75F, kEps);
    // Halton(1, 3) = 1/3; Halton(2, 3) = 2/3.
    EXPECT_NEAR(halton(1, 3), 1.0F / 3.0F, kEps);
    EXPECT_NEAR(halton(2, 3), 2.0F / 3.0F, kEps);
}

TEST(PostTaa, JitterStaysInUnitBox)
{
    for (std::uint32_t i = 0; i < 100; ++i)
    {
        const auto j = jitter_offset(i, 8);
        EXPECT_GE(j.x, -0.5F - kEps);
        EXPECT_LE(j.x,  0.5F + kEps);
        EXPECT_GE(j.y, -0.5F - kEps);
        EXPECT_LE(j.y,  0.5F + kEps);
    }
}

TEST(PostTaa, ClipAabbReturnsCentreWhenHistoryInside)
{
    const cd::math::Vec3f lo  { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f hi  { 1.0F, 1.0F, 1.0F };
    const cd::math::Vec3f c   { 0.5F, 0.5F, 0.5F };
    const cd::math::Vec3f h   { 0.6F, 0.6F, 0.6F };  // inside box
    const auto out = clip_aabb(lo, hi, c, h);
    // History inside AABB -> clipping returns history unchanged (t=1).
    EXPECT_NEAR(out.x, 0.6F, kEps);
}

TEST(PostTaa, ClipAabbClampsOutsideHistoryToBoundary)
{
    const cd::math::Vec3f lo  { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f hi  { 1.0F, 1.0F, 1.0F };
    const cd::math::Vec3f c   { 0.5F, 0.5F, 0.5F };
    const cd::math::Vec3f h   { 5.0F, 0.5F, 0.5F };  // way outside on X
    const auto out = clip_aabb(lo, hi, c, h);
    // After clipping, output's X is at the upper boundary (1.0).
    EXPECT_NEAR(out.x, 1.0F, kEps);
    EXPECT_NEAR(out.y, 0.5F, kEps);
}

TEST(PostTaa, NeighborhoodBoxMeanCentredOnUniform)
{
    std::array<cd::math::Vec3f, 9> n;
    for (auto& v : n) v = { 0.4F, 0.4F, 0.4F };
    const auto box = neighborhood_box(n, 1.0F);
    EXPECT_NEAR(box.lo.x, 0.4F, kEps);  // zero variance -> lo == hi
    EXPECT_NEAR(box.hi.x, 0.4F, kEps);
}

TEST(PostTaa, ResolveStableUnderZeroMotionPicksHistory)
{
    // Zero motion + same neighborhood -> heavily history-weighted blend.
    std::array<cd::math::Vec3f, 9> n;
    for (auto& v : n) v = { 0.5F, 0.5F, 0.5F };
    const cd::math::Vec3f curr  { 0.5F, 0.5F, 0.5F };
    const cd::math::Vec3f hist  { 0.5F, 0.5F, 0.5F };
    const auto out = resolve(curr, hist, n, /*motion_px=*/0.0F, {});
    EXPECT_NEAR(out.x, 0.5F, kEps);
}

TEST(PostTaa, ResolveLeansCurrentUnderHighMotion)
{
    // History 1.0, current 0.0, no neighborhood variance.
    std::array<cd::math::Vec3f, 9> n;
    for (auto& v : n) v = { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f curr  { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f hist  { 1.0F, 1.0F, 1.0F };
    Settings s {};
    // Motion = 1 px * 100 scale = saturated, blend = max_blend (0.5).
    const auto out = resolve(curr, hist, n, /*motion_px=*/1.0F, s);
    // Heavy motion -> blend ~0.5 of current (0) + 0.5 of clipped hist (0,
    // because hist clipped to box [0..0]). Both end up near 0.
    EXPECT_NEAR(out.x, 0.0F, 0.05F);
}

TEST(PostTaa, GlslKernelNonEmptyAndDeclaresClipFn)
{
    EXPECT_FALSE(cd::post::taa::kTaaResolveCS.empty());
    EXPECT_NE(cd::post::taa::kTaaResolveCS.find("clip_aabb"),
              std::string_view::npos);
}

// =============================================================================
// Edge / negative coverage (≥80→100).
// =============================================================================

TEST(PostTaa, DefaultSettingsAreKaris)
{
    const Settings s {};
    EXPECT_FLOAT_EQ(s.min_blend_factor, 0.04F);
    EXPECT_FLOAT_EQ(s.max_blend_factor, 0.50F);
    EXPECT_FLOAT_EQ(s.variance_clip_gamma, 1.0F);
    EXPECT_FLOAT_EQ(s.motion_blend_scale, 100.0F);
    EXPECT_EQ(s.jitter_phase_count, 8U);
}

TEST(PostTaa, HaltonZeroIndexIsZero)
{
    // Halton(0, b) terminates immediately -> 0 (the loop never runs).
    EXPECT_NEAR(halton(0, 2), 0.0F, kEps);
    EXPECT_NEAR(halton(0, 3), 0.0F, kEps);
}

TEST(PostTaa, JitterCyclesWithPhaseCount)
{
    // frame_index and frame_index+phase_count map to the same Halton index
    // (the helper does (frame % phase) + 1), so the jitter repeats exactly.
    const auto a = jitter_offset(2, 8);
    const auto b = jitter_offset(2 + 8, 8);
    EXPECT_NEAR(a.x, b.x, kEps);
    EXPECT_NEAR(a.y, b.y, kEps);
}

TEST(PostTaa, JitterIsNotConstantAcrossPhases)
{
    // Consecutive phases must differ (a constant jitter defeats TAA).
    const auto j0 = jitter_offset(0, 8);
    const auto j1 = jitter_offset(1, 8);
    const bool differs = std::abs(j0.x - j1.x) > kEps ||
                         std::abs(j0.y - j1.y) > kEps;
    EXPECT_TRUE(differs);
}

TEST(PostTaa, ClipAabbHistoryEqualsCentreIsStable)
{
    // Degenerate ray (history == centre): dir ~ 0, the t_for guard returns
    // 1.0 per-axis so the output is the centre (no NaN from /0).
    const cd::math::Vec3f lo { 0.0F, 0.0F, 0.0F };
    const cd::math::Vec3f hi { 1.0F, 1.0F, 1.0F };
    const cd::math::Vec3f c  { 0.5F, 0.5F, 0.5F };
    const auto out = clip_aabb(lo, hi, c, c);
    EXPECT_NEAR(out.x, 0.5F, kEps);
    EXPECT_NEAR(out.y, 0.5F, kEps);
    EXPECT_NEAR(out.z, 0.5F, kEps);
}

TEST(PostTaa, NeighborhoodBoxNonUniformHasPositiveExtent)
{
    // A spread of values yields lo < mean < hi (non-degenerate variance box).
    std::array<cd::math::Vec3f, 9> n {};
    for (std::size_t i = 0; i < n.size(); ++i)
    {
        const float v = static_cast<float>(i) * 0.1F;  // 0.0 .. 0.8
        n[i] = { v, v, v };
    }
    const auto box = neighborhood_box(n, 1.0F);
    EXPECT_LT(box.lo.x, box.hi.x);
    EXPECT_GT(box.hi.x - box.lo.x, kEps);
}

TEST(PostTaa, NeighborhoodBoxGammaWidensExtent)
{
    // A larger variance-clip gamma must widen the box (more tolerant clip).
    std::array<cd::math::Vec3f, 9> n {};
    for (std::size_t i = 0; i < n.size(); ++i)
    {
        const float v = static_cast<float>(i) * 0.1F;
        n[i] = { v, v, v };
    }
    const auto narrow = neighborhood_box(n, 1.0F);
    const auto wide   = neighborhood_box(n, 2.0F);
    EXPECT_GT(wide.hi.x - wide.lo.x, narrow.hi.x - narrow.lo.x);
}

TEST(PostTaa, ResolveZeroMotionLeansHistory)
{
    // Zero motion -> blend == min_blend_factor (0.04), so the output is
    // dominated by the (in-box) history sample, not the current frame.
    std::array<cd::math::Vec3f, 9> n {};
    for (auto& v : n) v = { 0.5F, 0.5F, 0.5F };
    const cd::math::Vec3f curr { 1.0F, 1.0F, 1.0F };
    const cd::math::Vec3f hist { 0.5F, 0.5F, 0.5F };  // inside the box
    Settings s {};
    const auto out = resolve(curr, hist, n, /*motion_px=*/0.0F, s);
    // out = curr*0.04 + clipped*0.96; clipped == hist (0.5) since in-box.
    EXPECT_NEAR(out.x, 1.0F * 0.04F + 0.5F * 0.96F, kEps);
}

}  // namespace
