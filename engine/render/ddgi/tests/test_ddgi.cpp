#include <cd/ddgi/Ddgi.hpp>

#include <gtest/gtest.h>

#include <array>

namespace
{

using cd::ddgi::GridConfig;
using cd::ddgi::probe_world_pos;
using cd::ddgi::trilinear_probe_weights;

constexpr float kEps = 1e-3F;

TEST(Ddgi, ProbeWorldPosOffsetsBySpacing)
{
    GridConfig g {};
    g.origin = { 1, 2, 3 };
    g.spacing = { 2, 2, 2 };
    const auto p = probe_world_pos(g, 1, 0, 0);
    EXPECT_NEAR(p.x, 3.0F, kEps);
    EXPECT_NEAR(p.y, 2.0F, kEps);
}

TEST(Ddgi, TrilinearWeightsSumToOneInside)
{
    GridConfig g {};
    std::array<float, 8> w;
    std::array<std::array<std::uint32_t, 3>, 8> c;
    trilinear_probe_weights(g, { 2.5F, 1.5F, 3.5F }, w, c);
    float sum = 0;
    for (float v : w) sum += v;
    EXPECT_NEAR(sum, 1.0F, kEps);
}

TEST(Ddgi, TrilinearWeightsZeroOutsideGrid)
{
    GridConfig g {};
    std::array<float, 8> w;
    std::array<std::array<std::uint32_t, 3>, 8> c;
    // Point past the grid extent (grid is 8x4x8 with unit spacing).
    trilinear_probe_weights(g, { 20, 20, 20 }, w, c);
    float sum = 0;
    for (float v : w) sum += v;
    EXPECT_NEAR(sum, 0.0F, kEps);
}

TEST(Ddgi, GlslProbeUpdateNonEmpty)
{
    EXPECT_FALSE(cd::ddgi::kProbeUpdateCS.empty());
    EXPECT_NE(cd::ddgi::kProbeUpdateCS.find("ray_query"),
              std::string_view::npos);
}

}  // namespace
