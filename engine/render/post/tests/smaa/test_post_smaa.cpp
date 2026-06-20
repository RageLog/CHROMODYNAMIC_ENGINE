#include <cd/post/smaa/Smaa.hpp>

#include <gtest/gtest.h>

#include <string_view>

namespace
{

using cd::post::smaa::luma_edge;
using cd::post::smaa::Settings;

TEST(PostSmaa, NoEdgeWhenAllLumasAreEqual)
{
    Settings s {};
    EXPECT_EQ(luma_edge(0.5F, 0.5F, 0.5F, 0.5F, 0.5F, s), 0U);
}

TEST(PostSmaa, HorizontalEdgeDetected)
{
    Settings s {};
    // Big luma jump to the left -> horizontal edge bit (bit 0) set.
    const auto m = luma_edge(0.9F, 0.1F, 0.9F, 0.9F, 0.9F, s);
    EXPECT_NE(m & 1u, 0u);
}

TEST(PostSmaa, VerticalEdgeDetected)
{
    Settings s {};
    // Big luma jump to the top -> vertical edge bit (bit 1) set.
    const auto m = luma_edge(0.9F, 0.9F, 0.1F, 0.9F, 0.9F, s);
    EXPECT_NE(m & 2u, 0u);
}

TEST(PostSmaa, GlslKernelsNonEmpty)
{
    EXPECT_FALSE(cd::post::smaa::kSmaaEdgeDetectCS.empty());
    EXPECT_FALSE(cd::post::smaa::kSmaaBlendCS.empty());
}

// =============================================================================
// Edge / negative coverage (≥80→100).
// =============================================================================

TEST(PostSmaa, DefaultSettingsAreJimenez)
{
    const Settings s {};
    EXPECT_FLOAT_EQ(s.threshold, 0.1F);
    EXPECT_FLOAT_EQ(s.adaptation_factor, 2.0F);
    EXPECT_EQ(s.max_search_steps, 16U);
}

TEST(PostSmaa, BothEdgesDetectedSimultaneously)
{
    // Large jump to BOTH left and top -> both bits set (0b11).
    Settings s {};
    const auto m = luma_edge(0.9F, 0.1F, 0.1F, 0.9F, 0.9F, s);
    EXPECT_NE(m & 1u, 0u);  // horizontal
    EXPECT_NE(m & 2u, 0u);  // vertical
    EXPECT_EQ(m, 3u);
}

TEST(PostSmaa, SubThresholdDeltaProducesNoEdge)
{
    // Delta of 0.02 sits under the effective threshold (0.1*0.5 = 0.05),
    // so no edge bit fires — guards against an over-eager detector.
    Settings s {};
    s.threshold = 0.1F;
    const auto m = luma_edge(0.50F, 0.48F, 0.52F, 0.50F, 0.50F, s);
    EXPECT_EQ(m, 0u);
}

TEST(PostSmaa, HigherThresholdSuppressesWeakEdge)
{
    // A delta that fired at threshold 0.1 must NOT fire at threshold 0.5.
    const float l_c = 0.6F;
    const float l_l = 0.5F;  // dh = 0.1
    Settings lo {};
    lo.threshold = 0.1F;     // effective 0.05 -> edge
    Settings hi {};
    hi.threshold = 0.5F;     // effective 0.25 -> no edge
    EXPECT_NE(luma_edge(l_c, l_l, l_c, l_c, l_c, lo) & 1u, 0u);
    EXPECT_EQ(luma_edge(l_c, l_l, l_c, l_c, l_c, hi) & 1u, 0u);
}

TEST(PostSmaa, ZeroThresholdStillHasEpsilonFloor)
{
    // threshold 0 -> effective t = max(0, 1e-3); identical lumas stay
    // edge-free (no divide-by-zero / spurious edge from the floor).
    Settings s {};
    s.threshold = 0.0F;
    EXPECT_EQ(luma_edge(0.5F, 0.5F, 0.5F, 0.5F, 0.5F, s), 0u);
    // A delta above the 1e-3 floor still registers.
    EXPECT_NE(luma_edge(0.5F, 0.4F, 0.5F, 0.5F, 0.5F, s) & 1u, 0u);
}

TEST(PostSmaa, EdgeDetectKernelComputesLuma)
{
    const std::string_view cs { cd::post::smaa::kSmaaEdgeDetectCS };
    EXPECT_NE(cs.find("luma"), std::string_view::npos);
    EXPECT_NE(cs.find("threshold"), std::string_view::npos);
}

}  // namespace
