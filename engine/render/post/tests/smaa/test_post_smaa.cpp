#include <cd/post/smaa/Smaa.hpp>

#include <gtest/gtest.h>

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

}  // namespace
