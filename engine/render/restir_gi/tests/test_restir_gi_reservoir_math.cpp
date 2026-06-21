// =============================================================================
// CHROMODYNAMIC -- engine/render/restir_gi/tests/test_restir_gi_reservoir_math.cpp
// Floor-raise marathon (38 -> ~70) -- host-side ReSTIR-GI reservoir math
// edge / negative / invariant coverage.
//
// ADD-ONLY. This file exercises the header-only WRS reservoir helpers in
// cd/restir_gi/GiReservoir.hpp (final_weight / update / combine /
// clamp_history / temporal_blend / invalidate) at their boundaries. None of
// these helpers touch the GPU or alter rendered output -- they are the
// host-side reference that the embedded GLSL kernels mirror (the same algebra
// as the proven cd::restir_di reservoir, which is at 100). The pre-existing
// test_restir_gi.cpp covers the happy path; this file pins the EXACT current
// behaviour at the boundaries the Ouyang 2021 / Bitterli 2020 RIS estimator
// relies on:
//
//   * final_weight: W = weight_sum / (M * target_pdf), M==0 and pdf<=0 guards;
//   * update: weight_sum<=0 short-circuit, exact swap-ratio threshold;
//   * combine: M==0 donor no-op, the M-cancellation w = p_hat*ws/pdf identity,
//     visibility re-test flip, additive M growth;
//   * clamp_history: strictly-greater guard (no-op at/under cap),
//     proportional scaling that preserves final_weight;
//   * temporal_blend: alpha endpoints + midpoint lerp + tie rule + age carry;
//   * the DEFERRED-TRACE seal markers in the GLSL sample kernel.
//
// The "deferred-trace" sample CS (synthesised bounce + constant cached
// radiance instead of a real ray query) is documented-incomplete and SEALED
// out of charter: a real GI gather needs G-buffer + TLAS ray-query + radiance
// cache wired by a render-graph consumer (the cd::restir_di::DispatchPass
// pattern), a multi-week RHI-dispatch subsystem -- exactly the same seal as
// cd::restir_di's Sprint-7 trace. These tests deliberately assert NOTHING
// about the traced radiance; they only lock the reservoir math + the seal
// markers so a refactor cannot silently drop the honest-scope banner.
//
// Anti-flakiness: every input is deterministic (no sleep_for, no RNG). Runs
// everywhere -- CPU-only header math, no Vulkan ICD needed.
// =============================================================================
#include <cd/restir_gi/GiReservoir.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

namespace
{

using cd::restir_gi::clamp_history;
using cd::restir_gi::combine;
using cd::restir_gi::Reservoir;
using cd::restir_gi::Sample;
using cd::restir_gi::temporal_blend;
using cd::restir_gi::update;

constexpr float kEps = 1e-4F;

// --- final_weight() boundaries ----------------------------------------------

TEST(RestirGiReservoirMath, FinalWeightZeroWhenMZero)
{
    // Ouyang/Bitterli Eq. denominator is M * target_pdf; an empty reservoir
    // (M == 0) must short-circuit to 0 rather than divide-by-zero.
    Reservoir r {};
    r.M          = 0U;
    r.weight_sum = 99.0F;
    EXPECT_NEAR(r.final_weight(1.0F), 0.0F, kEps);
}

TEST(RestirGiReservoirMath, FinalWeightZeroWhenTargetPdfNonPositive)
{
    // A zero / negative target_pdf must short-circuit to 0 (the guard is
    // target_pdf <= 0.0F), never divide.
    Reservoir r {};
    r.M          = 4U;
    r.weight_sum = 10.0F;
    EXPECT_NEAR(r.final_weight(0.0F), 0.0F, kEps);
    EXPECT_NEAR(r.final_weight(-2.0F), 0.0F, kEps);
}

TEST(RestirGiReservoirMath, FinalWeightMatchesClosedForm)
{
    // W = weight_sum / (M * target_pdf) = 20 / (5 * 2) = 2.0.
    Reservoir r {};
    r.M          = 5U;
    r.weight_sum = 20.0F;
    EXPECT_NEAR(r.final_weight(2.0F), 2.0F, kEps);
}

TEST(RestirGiReservoirMath, FinalWeightScalesInverselyWithTargetPdf)
{
    // Doubling target_pdf halves the per-sample estimate (1/(M*p_hat) form).
    Reservoir r {};
    r.M          = 2U;
    r.weight_sum = 8.0F;
    EXPECT_NEAR(r.final_weight(1.0F), 4.0F, kEps);  // 8 / (2*1)
    EXPECT_NEAR(r.final_weight(2.0F), 2.0F, kEps);  // 8 / (2*2)
}

// --- update() guards ---------------------------------------------------------

TEST(RestirGiReservoirMath, UpdateNegativeWeightStreamsButNeverSelects)
{
    // weight_sum <= 0 short-circuits before the ratio test: M still counts the
    // candidate (it streamed through) but no selection occurs off the default.
    Reservoir r {};
    Sample s {};
    s.point = { 4, 5, 6 };
    update(r, s, -5.0F, 0.0F);  // rand_01 = 0, smallest possible
    EXPECT_EQ(r.M, 1U);
    EXPECT_NEAR(r.weight_sum, -5.0F, kEps);
    // selected stayed at the default sentinel (point default is {0,0,0}).
    EXPECT_NEAR(r.selected.point.x, 0.0F, kEps);
}

TEST(RestirGiReservoirMath, UpdateSwapTakenWhenRandBelowRatio)
{
    // After a first unit-weight sample, a second unit-weight sample has ratio
    // w/weight_sum = 1/2; rand strictly below it flips the survivor.
    Reservoir r {};
    Sample first {};
    first.point = { 1, 0, 0 };
    Sample second {};
    second.point = { 2, 0, 0 };
    update(r, first, 1.0F, 0.9F);   // first always taken
    EXPECT_NEAR(r.selected.point.x, 1.0F, kEps);
    update(r, second, 1.0F, 0.25F);  // 0.25 < 0.5 -> flip
    EXPECT_NEAR(r.selected.point.x, 2.0F, kEps);
    EXPECT_EQ(r.M, 2U);
}

TEST(RestirGiReservoirMath, UpdateSwapRejectedWhenRandAtOrAboveRatio)
{
    // The comparison is strict (rand < ratio): rand exactly equal to the
    // ratio keeps the incumbent.
    Reservoir r {};
    Sample first {};
    first.point = { 1, 0, 0 };
    Sample second {};
    second.point = { 2, 0, 0 };
    update(r, first, 1.0F, 0.9F);
    update(r, second, 1.0F, 0.5F);  // 0.5 == ratio 0.5 -> NOT < -> keep first
    EXPECT_NEAR(r.selected.point.x, 1.0F, kEps);
    EXPECT_EQ(r.M, 2U);
}

TEST(RestirGiReservoirMath, UpdateCarriesIncomingRadianceOnSelectedSample)
{
    // The survivor carries the whole Sample, including the cached incoming
    // radiance that the RIS reuse later re-projects.
    Reservoir r {};
    Sample s {};
    s.point    = { 1, 2, 3 };
    s.incoming = { 0.4F, 0.5F, 0.6F };
    update(r, s, 1.0F, 0.0F);
    EXPECT_NEAR(r.selected.incoming.y, 0.5F, kEps);
    EXPECT_NEAR(r.selected.point.z, 3.0F, kEps);
}

// --- combine() boundaries ----------------------------------------------------

TEST(RestirGiReservoirMath, CombineWithEmptyDonorIsNoOp)
{
    // other.M == 0 -> early return; dst untouched (M, weight_sum, survivor).
    Reservoir dst {};
    Sample keep {};
    keep.point = { 5, 0, 0 };
    update(dst, keep, 1.0F, 0.0F);
    const std::uint32_t m_before          = dst.M;
    const float         weight_sum_before = dst.weight_sum;

    const Reservoir empty {};
    combine(dst, empty, 1.0F, 0.5F, [](const Sample&) { return 5.0F; });

    EXPECT_EQ(dst.M, m_before);
    EXPECT_NEAR(dst.weight_sum, weight_sum_before, kEps);
    EXPECT_NEAR(dst.selected.point.x, 5.0F, kEps);
}

TEST(RestirGiReservoirMath, CombineGrowsMAdditively)
{
    // dst.M grows by the donor's full M regardless of selection outcome.
    Reservoir dst {};
    update(dst, Sample {}, 1.0F, 0.0F);  // dst.M == 1
    Reservoir donor {};
    update(donor, Sample {}, 1.0F, 0.0F);
    update(donor, Sample {}, 1.0F, 0.0F);
    update(donor, Sample {}, 1.0F, 0.0F);  // donor.M == 3
    combine(dst, donor, 1.0F, 0.5F, [](const Sample&) { return 1.0F; });
    EXPECT_EQ(dst.M, 4U);  // 1 + 3
}

TEST(RestirGiReservoirMath, CombineZeroPHatAddsMButNotWeight)
{
    // A donor whose re-evaluated p_hat is 0 at the receiver contributes its M
    // (so normalisation tracks samples seen) but adds zero weight and can
    // never be selected.
    Reservoir dst {};
    Sample keep {};
    keep.point = { 3, 3, 3 };
    update(dst, keep, 1.0F, 0.0F);
    const float weight_before = dst.weight_sum;

    Reservoir donor {};
    update(donor, Sample {}, 1.0F, 0.0F);

    combine(dst, donor, 1.0F, 0.0F, [](const Sample&) { return 0.0F; });
    EXPECT_EQ(dst.M, 2U);
    EXPECT_NEAR(dst.weight_sum, weight_before, kEps);
    EXPECT_NEAR(dst.selected.point.x, 3.0F, kEps);  // never flipped
}

TEST(RestirGiReservoirMath, CombineWeightFollowsMCancellationIdentity)
{
    // The donor weight added is w = p_hat * final_weight(pdf) * M, and since
    // final_weight = weight_sum / (M * pdf) the M cancels:
    //     w = p_hat * weight_sum / pdf.
    // Pin that exact contribution. Start dst empty so weight_sum after the
    // combine equals exactly w.
    Reservoir dst {};                 // M==0, weight_sum==0
    Reservoir donor {};
    update(donor, Sample {}, 6.0F, 0.0F);  // donor.weight_sum = 6, M = 1
    // p_hat = 2, donor_target_pdf = 3  ->  w = 2 * 6 / 3 = 4.
    combine(dst, donor, 3.0F, 1.0F, [](const Sample&) { return 2.0F; });
    EXPECT_NEAR(dst.weight_sum, 4.0F, kEps);
    EXPECT_EQ(dst.M, 1U);
}

TEST(RestirGiReservoirMath, CombineWeightIndependentOfDonorMViaCancellation)
{
    // Because M cancels in w = p_hat * weight_sum / pdf, two donors with the
    // SAME weight_sum/pdf/p_hat but different M contribute the SAME weight.
    auto donor_with_m = [](std::uint32_t streams) {
        Reservoir d {};
        // Stream `streams` unit-weight candidates -> weight_sum == streams,
        // M == streams. To isolate the cancellation, instead set fields so
        // weight_sum is fixed independent of M.
        for (std::uint32_t i = 0; i < streams; ++i)
            update(d, Sample {}, 0.0F, 0.0F);  // M grows, weight_sum stays 0
        d.weight_sum = 6.0F;                    // fixed weight_sum
        return d;
    };

    Reservoir a = donor_with_m(2U);
    Reservoir b = donor_with_m(5U);
    ASSERT_EQ(a.weight_sum, b.weight_sum);
    ASSERT_NE(a.M, b.M);

    Reservoir dst_a {};
    Reservoir dst_b {};
    combine(dst_a, a, 3.0F, 1.0F, [](const Sample&) { return 2.0F; });
    combine(dst_b, b, 3.0F, 1.0F, [](const Sample&) { return 2.0F; });
    // w = 2 * 6 / 3 = 4 for both, independent of M.
    EXPECT_NEAR(dst_a.weight_sum, 4.0F, kEps);
    EXPECT_NEAR(dst_b.weight_sum, 4.0F, kEps);
}

TEST(RestirGiReservoirMath, CombineSelectsDonorAndFlipsVisibility)
{
    // Force the swap (huge p_hat + rand just under 1) and verify the donor
    // survivor's visibility is flipped to 0 -- Ouyang requires a re-test of
    // the reused secondary hit's visibility before it is shaded.
    Reservoir dst {};
    Sample local {};
    local.valid = 1;
    update(dst, local, 1.0F, 0.0F);

    Reservoir donor {};
    Sample remote {};
    remote.point = { 7, 7, 7 };
    remote.valid = 1;
    update(donor, remote, 1.0F, 0.0F);

    combine(dst, donor, 1.0F, 0.999F, [](const Sample&) { return 1.0e6F; });
    EXPECT_NEAR(dst.selected.point.x, 7.0F, kEps);
    EXPECT_EQ(dst.selected.valid, 0U);  // visibility marked for re-test
    EXPECT_EQ(dst.M, 2U);
}

TEST(RestirGiReservoirMath, CombineDonorWithNonPositivePdfContributesZeroWeight)
{
    // other.final_weight(pdf<=0) returns 0, so w = p_hat * 0 * M = 0: the
    // donor folds its M in but cannot move weight or survivor even with a
    // large p_hat.
    Reservoir dst {};
    Sample keep {};
    keep.point = { 8, 8, 8 };
    update(dst, keep, 1.0F, 0.0F);
    const float weight_before = dst.weight_sum;

    Reservoir donor {};
    update(donor, Sample {}, 4.0F, 0.0F);  // donor has weight, M==1

    combine(dst, donor, 0.0F, 0.0F, [](const Sample&) { return 1.0e6F; });
    EXPECT_NEAR(dst.weight_sum, weight_before, kEps);  // no weight added
    EXPECT_EQ(dst.M, 2U);                              // M still folds in
    EXPECT_NEAR(dst.selected.point.x, 8.0F, kEps);     // survivor unchanged
}

// --- clamp_history() boundaries ----------------------------------------------

TEST(RestirGiReservoirMath, ClampHistoryNoOpWhenUnderCap)
{
    Reservoir r {};
    r.M          = 50U;
    r.weight_sum = 123.0F;
    clamp_history(r, 100U);  // cap above current M -> untouched
    EXPECT_EQ(r.M, 50U);
    EXPECT_NEAR(r.weight_sum, 123.0F, kEps);
}

TEST(RestirGiReservoirMath, ClampHistoryNoOpAtExactCap)
{
    // The guard is strictly-greater (r.M > cap); M == cap is a no-op.
    Reservoir r {};
    r.M          = 100U;
    r.weight_sum = 200.0F;
    clamp_history(r, 100U);
    EXPECT_EQ(r.M, 100U);
    EXPECT_NEAR(r.weight_sum, 200.0F, kEps);
}

TEST(RestirGiReservoirMath, ClampHistoryScalesWeightProportionally)
{
    // Clamping M scales weight_sum by cap/M.
    Reservoir r {};
    r.M          = 400U;
    r.weight_sum = 800.0F;
    clamp_history(r, 100U);
    EXPECT_EQ(r.M, 100U);
    EXPECT_NEAR(r.weight_sum, 200.0F, kEps);  // 800 * 100/400
}

TEST(RestirGiReservoirMath, ClampHistoryPreservesFinalWeight)
{
    // The whole point of the proportional clamp: the per-sample estimate
    // final_weight is invariant under the cap (weight_sum and M scale by the
    // same factor).
    Reservoir r {};
    r.M          = 600U;
    r.weight_sum = 300.0F;
    const float fw_before = r.final_weight(1.5F);
    clamp_history(r, 20U);
    EXPECT_EQ(r.M, 20U);
    EXPECT_NEAR(r.final_weight(1.5F), fw_before, kEps);
}

// --- temporal_blend() endpoints + interior ----------------------------------

TEST(RestirGiReservoirMath, TemporalBlendAlphaZeroIsPureCurrent)
{
    Reservoir cur {};
    cur.weight_sum     = 10.0F;
    cur.M              = 5U;
    cur.selected.point = { 1, 0, 0 };
    Reservoir prev {};
    prev.weight_sum     = 99.0F;
    prev.M              = 50U;
    prev.selected.point = { 9, 0, 0 };

    const Reservoir r = temporal_blend(cur, prev, 0.0F);
    EXPECT_NEAR(r.weight_sum, 10.0F, kEps);
    EXPECT_EQ(r.M, 5U);
    EXPECT_NEAR(r.selected.point.x, 1.0F, kEps);  // alpha<=0.5 -> current
}

TEST(RestirGiReservoirMath, TemporalBlendAlphaOneIsPurePrevious)
{
    Reservoir cur {};
    cur.weight_sum     = 10.0F;
    cur.M              = 5U;
    cur.selected.point = { 1, 0, 0 };
    Reservoir prev {};
    prev.weight_sum     = 99.0F;
    prev.M              = 50U;
    prev.selected.point = { 9, 0, 0 };

    const Reservoir r = temporal_blend(cur, prev, 1.0F);
    EXPECT_NEAR(r.weight_sum, 99.0F, kEps);
    EXPECT_EQ(r.M, 50U);
    EXPECT_NEAR(r.selected.point.x, 9.0F, kEps);  // alpha>0.5 -> previous
}

TEST(RestirGiReservoirMath, TemporalBlendTieAtHalfTakesCurrentSurvivor)
{
    // The survivor tie rule is alpha <= 0.5 -> current. Pin the exact
    // boundary at alpha == 0.5.
    Reservoir cur {};
    cur.selected.point = { 1, 0, 0 };
    Reservoir prev {};
    prev.selected.point = { 9, 0, 0 };
    const Reservoir r = temporal_blend(cur, prev, 0.5F);
    EXPECT_NEAR(r.selected.point.x, 1.0F, kEps);  // tie -> current
}

TEST(RestirGiReservoirMath, TemporalBlendMidpointLerpsWeightAndM)
{
    Reservoir cur {};
    cur.weight_sum = 10.0F;
    cur.M          = 4U;
    Reservoir prev {};
    prev.weight_sum = 30.0F;
    prev.M          = 8U;

    const Reservoir r = temporal_blend(cur, prev, 0.5F);
    EXPECT_NEAR(r.weight_sum, 20.0F, kEps);  // 0.5*10 + 0.5*30
    EXPECT_EQ(r.M, 6U);                       // 0.5*4 + 0.5*8
}

TEST(RestirGiReservoirMath, TemporalBlendTruncatesFractionalMTowardZero)
{
    // M blends in float then casts to uint32 (truncation). 0.5*5 + 0.5*4 =
    // 4.5 -> truncates to 4, not rounds to 5. Pin the truncation behaviour.
    Reservoir cur {};
    cur.M = 5U;
    Reservoir prev {};
    prev.M = 4U;
    const Reservoir r = temporal_blend(cur, prev, 0.5F);
    EXPECT_EQ(r.M, 4U);
}

TEST(RestirGiReservoirMath, TemporalBlendPropagatesCurrentAge)
{
    Reservoir cur {};
    cur.age = 7U;
    Reservoir prev {};
    prev.age = 99U;
    const Reservoir r = temporal_blend(cur, prev, 0.8F);
    EXPECT_EQ(r.age, 7U);  // age tracks the CURRENT reservoir, not previous
}

// --- invalidate() ------------------------------------------------------------

TEST(RestirGiReservoirMath, InvalidateResetsEverythingToZeroState)
{
    Reservoir r {};
    r.M                = 17U;
    r.weight_sum       = 42.0F;
    r.age              = 3U;
    r.selected.point   = { 5, 5, 5 };
    r.selected.valid   = 1;

    r.invalidate();

    EXPECT_EQ(r.M, 0U);
    EXPECT_NEAR(r.weight_sum, 0.0F, kEps);
    EXPECT_EQ(r.age, 0U);
    EXPECT_NEAR(r.selected.point.x, 0.0F, kEps);
    EXPECT_NEAR(r.final_weight(1.0F), 0.0F, kEps);
}

// --- GLSL string presence + DEFERRED-TRACE seal -----------------------------
// NOTE: these only assert the kernels still EXIST + expose their key symbols
// and that the honest-scope seal markers survive. A substring presence check
// cannot change rendered output; it trips a test if a kernel or seal banner is
// silently removed (the contract that "GI is not actually traced" must stay
// visible to anyone reading the embedded shader).

TEST(RestirGiReservoirMath, GlslHelperExposesGiReservoirFns)
{
    EXPECT_FALSE(cd::restir_gi::kGiReservoirGlsl.empty());
    EXPECT_NE(cd::restir_gi::kGiReservoirGlsl.find("gi_reservoir_update"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_gi::kGiReservoirGlsl.find("gi_reservoir_final_weight"),
              std::string_view::npos);
}

TEST(RestirGiReservoirMath, GlslSampleKernelExposesWrsLoopAndTlasBinding)
{
    EXPECT_FALSE(cd::restir_gi::kRestirGiSampleCS.empty());
    EXPECT_NE(cd::restir_gi::kRestirGiSampleCS.find("local_size_x = 8"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_gi::kRestirGiSampleCS.find("pc.candidates"),
              std::string_view::npos);
    // The TLAS binding is declared even though the deferred-trace block does
    // not yet issue a ray query against it.
    EXPECT_NE(cd::restir_gi::kRestirGiSampleCS.find("u_TLAS"),
              std::string_view::npos);
}

TEST(RestirGiReservoirMath, SampleKernelKeepsDeferredTraceSeal)
{
    // Honest-scope seal: the sample kernel MUST keep the DEFERRED-TRACE marker
    // so the "GI is not actually traced; promote-on-need" contract stays
    // visible. A real GI gather (ray-query + radiance cache) is a multi-week
    // RHI-dispatch subsystem (the cd::restir_di::DispatchPass pattern) and is
    // sealed out of this header's stated scope.
    EXPECT_NE(cd::restir_gi::kRestirGiSampleCS.find("DEFERRED TRACE"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_gi::kRestirGiSampleCS.find("deferred-trace hit point"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_gi::kRestirGiSampleCS.find("deferred-trace cached radiance"),
              std::string_view::npos);
}

TEST(RestirGiReservoirMath, GlslTemporalKernelExposesMCapAndMotionVectors)
{
    EXPECT_FALSE(cd::restir_gi::kRestirGiTemporalReuseCS.empty());
    EXPECT_NE(cd::restir_gi::kRestirGiTemporalReuseCS.find("M_cap"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_gi::kRestirGiTemporalReuseCS.find("u_MotionVectors"),
              std::string_view::npos);
}

TEST(RestirGiReservoirMath, GlslSpatialKernelExposesTapsAndRadius)
{
    EXPECT_FALSE(cd::restir_gi::kRestirGiSpatialReuseCS.empty());
    EXPECT_NE(cd::restir_gi::kRestirGiSpatialReuseCS.find("spatial_taps"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_gi::kRestirGiSpatialReuseCS.find("spatial_radius"),
              std::string_view::npos);
}

}  // namespace
