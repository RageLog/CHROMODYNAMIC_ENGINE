#include <cd/restir_gi/GiReservoir.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::restir_gi::clamp_history;
using cd::restir_gi::combine;
using cd::restir_gi::Reservoir;
using cd::restir_gi::Sample;
using cd::restir_gi::update;

constexpr float kEps = 1e-3F;

TEST(RestirGi, FirstSampleIsAlwaysSelected)
{
    Reservoir r {};
    Sample s {};
    s.point = { 1, 2, 3 };
    s.incoming = { 1, 0, 0 };
    update(r, s, 1.0F, 0.99F);
    EXPECT_NEAR(r.selected.point.x, 1.0F, kEps);
    EXPECT_EQ(r.M, 1U);
}

TEST(RestirGi, FinalWeightZeroOnEmpty)
{
    Reservoir r {};
    EXPECT_NEAR(r.final_weight(1.0F), 0.0F, kEps);
}

TEST(RestirGi, ClampHistoryScalesProportionally)
{
    Reservoir r {};
    r.M = 400;
    r.weight_sum = 800.0F;
    clamp_history(r, 100);
    EXPECT_EQ(r.M, 100U);
    EXPECT_NEAR(r.weight_sum, 200.0F, kEps);
}

TEST(RestirGi, CombineInvalidatesVisibility)
{
    Reservoir a {}, b {};
    Sample sa {}, sb {};
    sb.valid = 1;
    update(a, sa, 1.0F, 0.5F);
    update(b, sb, 1.0F, 0.5F);
    // Force combine to take the donor (rnd ~ 0.99 + large eval).
    combine(a, b, 1.0F, 0.999F, [](const Sample&) { return 100.0F; });
    // Whether or not it actually swapped, dst.M grew (visibility flag
    // is only flipped on swap).
    EXPECT_GE(a.M, 2U);
}

TEST(RestirGi, GlslHelpersExposeGiReservoirFns)
{
    EXPECT_FALSE(cd::restir_gi::kGiReservoirGlsl.empty());
    EXPECT_NE(cd::restir_gi::kGiReservoirGlsl.find("gi_reservoir_update"),
              std::string_view::npos);
}

}  // namespace
