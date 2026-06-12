#include <cd/restir_di/Reservoir.hpp>

#include <gtest/gtest.h>

#include <random>

namespace
{

using cd::restir_di::clamp_history;
using cd::restir_di::combine;
using cd::restir_di::Reservoir;
using cd::restir_di::Sample;
using cd::restir_di::update;

constexpr float kEps = 1e-3F;

TEST(RestirDi, FinalWeightZeroOnEmptyReservoir)
{
    Reservoir r {};
    EXPECT_NEAR(r.final_weight(), 0.0F, kEps);
}

TEST(RestirDi, UpdateTakesFirstSampleUnconditionally)
{
    Reservoir r {};
    Sample s { 7, { 1, 1, 1 }, 1.0F };
    update(r, s, 1.0F, 0.99F);  // any rnd ok — first sample always selected
    EXPECT_EQ(r.selected.light_index, 7U);
    EXPECT_EQ(r.m, 1U);
}

TEST(RestirDi, UpdateProbabilityMatchesWeightRatio)
{
    // Stream 1000 samples with weight 1.0 then weight 3.0; the
    // second sample should be selected ~75% of the time. Use a
    // deterministic RNG to assert on the empirical fraction.
    std::mt19937 rng(123);
    std::uniform_real_distribution<float> u(0.0F, 1.0F);
    int picked_b = 0;
    for (int trial = 0; trial < 1000; ++trial)
    {
        Reservoir r {};
        Sample a { 1, {}, 1.0F };
        Sample b { 2, {}, 1.0F };
        update(r, a, 1.0F, u(rng));
        update(r, b, 3.0F, u(rng));
        if (r.selected.light_index == 2U) ++picked_b;
    }
    EXPECT_GT(picked_b, 700);
    EXPECT_LT(picked_b, 800);
}

TEST(RestirDi, ClampHistoryLimitsM)
{
    Reservoir r {};
    r.m = 500;
    r.weight_sum = 1000.0F;
    clamp_history(r, 100);
    EXPECT_EQ(r.m, 100U);
    EXPECT_NEAR(r.weight_sum, 200.0F, kEps);
}

TEST(RestirDi, CombineMergesDonorContribution)
{
    Reservoir a {};
    Reservoir b {};
    update(a, Sample { 1, { 1, 0, 0 }, 1.0F }, 1.0F, 0.5F);
    update(b, Sample { 2, { 0, 1, 0 }, 1.0F }, 1.0F, 0.5F);
    combine(a, b, 0.99F, [](const Sample& s) { return s.target_pdf; });
    EXPECT_GE(a.m, 2U);
    EXPECT_GE(a.weight_sum, 0.0F);
}

TEST(RestirDi, GlslHelpersExposeReservoirFns)
{
    EXPECT_FALSE(cd::restir_di::kReservoirGlsl.empty());
    EXPECT_NE(cd::restir_di::kReservoirGlsl.find("reservoir_update"),
              std::string_view::npos);
}

}  // namespace
