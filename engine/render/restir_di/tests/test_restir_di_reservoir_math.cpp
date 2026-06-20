// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_di/tests/test_restir_di_reservoir_math.cpp
// 80->100 marathon -- host-side reservoir math edge / negative coverage.
//
// ADD-ONLY: this file exercises the header-only WRS reservoir helpers in
// cd/restir_di/Reservoir.hpp (final_weight / update / combine / clamp_history /
// temporal_blend / invalidate) at their boundaries. None of these helpers
// touch the GPU or alter rendered output -- they are the host-side reference
// that the GLSL kernels mirror. The pre-existing test_restir_di.cpp covers the
// happy path; this file pins the edge cases the Bitterli 2020 estimator relies
// on (M==0 no-op, target_pdf<=0 guard, weight_sum<=0 early-return, M-cap
// no-op-when-under-cap, temporal_blend alpha endpoints).
//
// Runs everywhere -- no Vulkan ICD needed (CPU-only header math).
// =============================================================================
#include <cd/restir_di/Reservoir.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace
{

using cd::restir_di::clamp_history;
using cd::restir_di::combine;
using cd::restir_di::Reservoir;
using cd::restir_di::Sample;
using cd::restir_di::temporal_blend;
using cd::restir_di::update;

constexpr float kEps = 1e-4F;

// --- final_weight() boundaries ----------------------------------------------

TEST(RestirDiReservoirMath, FinalWeightZeroWhenTargetPdfNonPositive)
{
    // Bitterli Eq. 6 denominator is M * target_pdf; a zero / negative
    // target_pdf must short-circuit to 0 rather than divide-by-zero.
    Reservoir r {};
    r.m          = 4U;
    r.weight_sum = 10.0F;
    r.selected.target_pdf = 0.0F;
    EXPECT_NEAR(r.final_weight(), 0.0F, kEps);

    r.selected.target_pdf = -2.0F;
    EXPECT_NEAR(r.final_weight(), 0.0F, kEps);
}

TEST(RestirDiReservoirMath, FinalWeightMatchesClosedForm)
{
    // final_weight = weight_sum / (M * target_pdf).
    Reservoir r {};
    r.m          = 5U;
    r.weight_sum = 20.0F;
    r.selected.target_pdf = 2.0F;
    // 20 / (5 * 2) = 2.0
    EXPECT_NEAR(r.final_weight(), 2.0F, kEps);
}

TEST(RestirDiReservoirMath, FinalWeightZeroWhenMZero)
{
    Reservoir r {};
    r.m          = 0U;
    r.weight_sum = 99.0F;
    r.selected.target_pdf = 1.0F;
    EXPECT_NEAR(r.final_weight(), 0.0F, kEps);
}

// --- update() guards ---------------------------------------------------------

TEST(RestirDiReservoirMath, UpdateZeroWeightStreamsButNeverSelects)
{
    // A zero-weight candidate increments M (it streamed through) but with
    // weight_sum staying 0 the selection branch is skipped, so the survivor
    // never changes off the default sentinel.
    Reservoir r {};
    const Sample s { 9, { 1, 1, 1 }, 1.0F };
    update(r, s, 0.0F, 0.0F);  // rand_01 = 0, smallest possible
    EXPECT_EQ(r.m, 1U);
    EXPECT_NEAR(r.weight_sum, 0.0F, kEps);
    EXPECT_EQ(r.selected.light_index, 0U);  // unchanged sentinel
}

TEST(RestirDiReservoirMath, UpdateNegativeWeightDoesNotSelect)
{
    // weight_sum <= 0 short-circuits before the ratio test (no UB / no
    // selection on a pathological negative weight).
    Reservoir r {};
    const Sample s { 3, {}, 1.0F };
    update(r, s, -5.0F, 0.0F);
    EXPECT_EQ(r.m, 1U);
    EXPECT_EQ(r.selected.light_index, 0U);  // unchanged
}

TEST(RestirDiReservoirMath, UpdateSecondSampleSelectedWhenRandBelowRatio)
{
    // After the first unit-weight sample, a second unit-weight sample has
    // ratio w/weight_sum = 1/2; rand below that flips the survivor.
    Reservoir r {};
    update(r, Sample { 1, {}, 1.0F }, 1.0F, 0.9F);  // first always taken
    EXPECT_EQ(r.selected.light_index, 1U);
    update(r, Sample { 2, {}, 1.0F }, 1.0F, 0.25F);  // 0.25 < 0.5 -> flip
    EXPECT_EQ(r.selected.light_index, 2U);
    EXPECT_EQ(r.m, 2U);
}

TEST(RestirDiReservoirMath, UpdateSecondSampleRejectedWhenRandAboveRatio)
{
    Reservoir r {};
    update(r, Sample { 1, {}, 1.0F }, 1.0F, 0.9F);
    update(r, Sample { 2, {}, 1.0F }, 1.0F, 0.75F);  // 0.75 > 0.5 -> keep 1
    EXPECT_EQ(r.selected.light_index, 1U);
    EXPECT_EQ(r.m, 2U);
}

// --- combine() boundaries ----------------------------------------------------

TEST(RestirDiReservoirMath, CombineWithEmptyDonorIsNoOp)
{
    // other.m == 0 -> early return; dst untouched.
    Reservoir dst {};
    update(dst, Sample { 5, { 1, 0, 0 }, 1.0F }, 1.0F, 0.5F);
    const Reservoir before = dst;

    const Reservoir empty {};
    combine(dst, empty, 0.0F, [](const Sample& s) { return s.target_pdf; });

    EXPECT_EQ(dst.m, before.m);
    EXPECT_NEAR(dst.weight_sum, before.weight_sum, kEps);
    EXPECT_EQ(dst.selected.light_index, before.selected.light_index);
}

TEST(RestirDiReservoirMath, CombineZeroPHatAddsMButNotWeight)
{
    // A donor whose p_hat evaluates to 0 in the receiver domain contributes
    // its M (so the estimator's normalisation tracks samples seen) but adds
    // zero weight and can never be selected.
    Reservoir dst {};
    update(dst, Sample { 1, { 1, 0, 0 }, 1.0F }, 1.0F, 0.5F);
    const float dst_weight_before = dst.weight_sum;

    Reservoir donor {};
    update(donor, Sample { 2, { 0, 1, 0 }, 1.0F }, 1.0F, 0.5F);

    combine(dst, donor, 0.0F, [](const Sample&) { return 0.0F; });

    EXPECT_EQ(dst.m, 2U);                              // donor M folded in
    EXPECT_NEAR(dst.weight_sum, dst_weight_before, kEps);
    EXPECT_EQ(dst.selected.light_index, 1U);           // never flipped
}

TEST(RestirDiReservoirMath, CombineSelectsDonorWhenRandLow)
{
    // p_hat = 1, donor final_weight = 1, donor M = 1 -> donor weight w = 1.
    // dst started with weight 1, so after the add weight_sum = 2 and the
    // ratio w/weight_sum = 0.5; rand below it flips the survivor to donor.
    Reservoir dst {};
    update(dst, Sample { 1, { 1, 0, 0 }, 1.0F }, 1.0F, 0.5F);

    Reservoir donor {};
    update(donor, Sample { 2, { 0, 1, 0 }, 1.0F }, 1.0F, 0.5F);

    combine(dst, donor, 0.1F, [](const Sample& s) { return s.target_pdf; });
    EXPECT_EQ(dst.selected.light_index, 2U);
    EXPECT_EQ(dst.m, 2U);
    EXPECT_NEAR(dst.weight_sum, 2.0F, kEps);
}

// --- clamp_history() boundaries ----------------------------------------------

TEST(RestirDiReservoirMath, ClampHistoryNoOpWhenUnderCap)
{
    Reservoir r {};
    r.m          = 50U;
    r.weight_sum = 100.0F;
    clamp_history(r, 100U);  // cap above current M -> no change
    EXPECT_EQ(r.m, 50U);
    EXPECT_NEAR(r.weight_sum, 100.0F, kEps);
}

TEST(RestirDiReservoirMath, ClampHistoryNoOpAtExactCap)
{
    Reservoir r {};
    r.m          = 100U;
    r.weight_sum = 200.0F;
    clamp_history(r, 100U);  // M == cap -> strictly-greater guard is false
    EXPECT_EQ(r.m, 100U);
    EXPECT_NEAR(r.weight_sum, 200.0F, kEps);
}

TEST(RestirDiReservoirMath, ClampHistoryScalesWeightProportionally)
{
    // Bitterli §4.3: clamping M scales weight_sum by cap/M so final_weight
    // (the per-sample estimate) is preserved across the clamp.
    Reservoir r {};
    r.m          = 400U;
    r.weight_sum = 800.0F;
    r.selected.target_pdf = 1.0F;
    const float fw_before = r.final_weight();  // 800 / (400 * 1) = 2.0
    clamp_history(r, 100U);
    EXPECT_EQ(r.m, 100U);
    EXPECT_NEAR(r.weight_sum, 200.0F, kEps);   // 800 * 100/400
    EXPECT_NEAR(r.final_weight(), fw_before, kEps);  // preserved: 200/(100*1)
}

// --- temporal_blend() endpoints ---------------------------------------------

TEST(RestirDiReservoirMath, TemporalBlendAlphaZeroIsPureCurrent)
{
    Reservoir cur {};
    cur.selected   = Sample { 1, { 1, 0, 0 }, 1.0F };
    cur.weight_sum = 10.0F;
    cur.m          = 4U;

    Reservoir prev {};
    prev.selected   = Sample { 2, { 0, 1, 0 }, 1.0F };
    prev.weight_sum = 99.0F;
    prev.m          = 40U;

    const Reservoir b = temporal_blend(cur, prev, 0.0F);
    EXPECT_EQ(b.selected.light_index, 1U);       // current survivor
    EXPECT_NEAR(b.weight_sum, 10.0F, kEps);      // pure current weight
    EXPECT_EQ(b.m, 4U);                          // pure current M
}

TEST(RestirDiReservoirMath, TemporalBlendAlphaOneIsPurePrevious)
{
    Reservoir cur {};
    cur.selected   = Sample { 1, { 1, 0, 0 }, 1.0F };
    cur.weight_sum = 10.0F;
    cur.m          = 4U;

    Reservoir prev {};
    prev.selected   = Sample { 2, { 0, 1, 0 }, 1.0F };
    prev.weight_sum = 99.0F;
    prev.m          = 40U;

    const Reservoir b = temporal_blend(cur, prev, 1.0F);
    EXPECT_EQ(b.selected.light_index, 2U);       // previous survivor
    EXPECT_NEAR(b.weight_sum, 99.0F, kEps);      // pure previous weight
    EXPECT_EQ(b.m, 40U);                         // pure previous M
}

TEST(RestirDiReservoirMath, TemporalBlendHalfwayInterpolatesWeightAndM)
{
    Reservoir cur {};
    cur.selected   = Sample { 1, {}, 1.0F };
    cur.weight_sum = 10.0F;
    cur.m          = 4U;

    Reservoir prev {};
    prev.selected   = Sample { 2, {}, 1.0F };
    prev.weight_sum = 20.0F;
    prev.m          = 8U;

    const Reservoir b = temporal_blend(cur, prev, 0.5F);
    EXPECT_NEAR(b.weight_sum, 15.0F, kEps);  // 0.5*10 + 0.5*20
    EXPECT_EQ(b.m, 6U);                      // 0.5*4 + 0.5*8 = 6
    // alpha <= 0.5 -> current survivor per the implementation's tie rule.
    EXPECT_EQ(b.selected.light_index, 1U);
}

TEST(RestirDiReservoirMath, TemporalBlendPropagatesCurrentAge)
{
    Reservoir cur {};
    cur.age = 7U;
    Reservoir prev {};
    prev.age = 99U;
    const Reservoir b = temporal_blend(cur, prev, 0.8F);
    EXPECT_EQ(b.age, 7U);  // age tracks the CURRENT reservoir, not previous
}

// --- invalidate() ------------------------------------------------------------

TEST(RestirDiReservoirMath, InvalidateResetsToZeroState)
{
    Reservoir r {};
    r.selected   = Sample { 13, { 5, 5, 5 }, 9.0F };
    r.weight_sum = 42.0F;
    r.m          = 17U;
    r.age        = 3U;

    r.invalidate();

    EXPECT_EQ(r.selected.light_index, 0U);
    EXPECT_NEAR(r.weight_sum, 0.0F, kEps);
    EXPECT_EQ(r.m, 0U);
    EXPECT_EQ(r.age, 0U);
    EXPECT_NEAR(r.final_weight(), 0.0F, kEps);
}

// --- GLSL string presence (lock against accidental deletion) ----------------
// NOTE: these only assert the kernels still EXIST + expose their key symbols.
// They do NOT inspect or alter any math -- a substring presence check cannot
// change rendered output, but it trips a test if a kernel is silently removed.

TEST(RestirDiReservoirMath, GlslSampleKernelExposesWrsLoop)
{
    EXPECT_FALSE(cd::restir_di::kRestirDiSampleCS.empty());
    EXPECT_NE(cd::restir_di::kRestirDiSampleCS.find("local_size_x = 8"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_di::kRestirDiSampleCS.find("pc.candidates"),
              std::string_view::npos);
}

TEST(RestirDiReservoirMath, GlslTemporalKernelExposesMCap)
{
    EXPECT_FALSE(cd::restir_di::kRestirDiTemporalReuseCS.empty());
    EXPECT_NE(cd::restir_di::kRestirDiTemporalReuseCS.find("M_cap"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_di::kRestirDiTemporalReuseCS.find("u_MotionVectors"),
              std::string_view::npos);
}

TEST(RestirDiReservoirMath, GlslSpatialKernelExposesTaps)
{
    EXPECT_FALSE(cd::restir_di::kRestirDiSpatialReuseCS.empty());
    EXPECT_NE(cd::restir_di::kRestirDiSpatialReuseCS.find("spatial_taps"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_di::kRestirDiSpatialReuseCS.find("spatial_radius"),
              std::string_view::npos);
}

}  // namespace
