// =============================================================================
// CHROMODYNAMIC — cd::restir_gi tests (BAND-7 hardened).
//
// These cases lock the *reservoir-resampling-math-v1* core that BAND-7
// SEALs (docs/ADR/ADR-20260616-band7-scope.md §1): the WRS streaming
// update, the cross-pixel reservoir combine (RIS estimator), the history
// clamp, the temporal age cap + blend, and the GLSL helper exposure.
//
// They deliberately do NOT assert anything about the GI *trace* (the
// deferred-trace block in kRestirGiSampleCS) — that is a promote-on-need
// render-graph subsystem, not part of this header's sealed scope.
// Anti-flakiness: every input is deterministic (no sleep_for, no RNG).
// =============================================================================
#include <cd/restir_gi/GiReservoir.hpp>

#include <gtest/gtest.h>

namespace
{

using cd::restir_gi::clamp_history;
using cd::restir_gi::combine;
using cd::restir_gi::Reservoir;
using cd::restir_gi::Sample;
using cd::restir_gi::temporal_blend;
using cd::restir_gi::update;

constexpr float kEps = 1e-3F;

// ---------------------------------------------------------------------------
// WRS streaming update
// ---------------------------------------------------------------------------

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

TEST(RestirGi, UpdateAccumulatesWeightSumAndM)
{
    // WRS invariant: weight_sum is the running total of all streamed
    // weights and M counts every candidate, regardless of which survives.
    Reservoir r {};
    update(r, Sample {}, 2.0F, 0.10F);
    update(r, Sample {}, 3.0F, 0.10F);
    update(r, Sample {}, 5.0F, 0.10F);
    EXPECT_EQ(r.M, 3U);
    EXPECT_NEAR(r.weight_sum, 10.0F, kEps);
}

TEST(RestirGi, UpdateZeroWeightCountsButNeverSelects)
{
    // A zero-weight candidate still increments M (it streamed through) but
    // can never replace the survivor: the WRS test rand < w/weight_sum is
    // rand < 0 which is false for any rand in [0,1].
    Reservoir r {};
    Sample keep {};
    keep.point = { 9, 9, 9 };
    update(r, keep, 4.0F, 0.0F);   // selected on first stream
    Sample drop {};
    drop.point = { -1, -1, -1 };
    update(r, drop, 0.0F, 0.0F);   // weight 0 -> must not swap
    EXPECT_EQ(r.M, 2U);
    EXPECT_NEAR(r.weight_sum, 4.0F, kEps);
    EXPECT_NEAR(r.selected.point.x, 9.0F, kEps);
}

TEST(RestirGi, Rand01BelowOneAlwaysTakesSecondHeavySample)
{
    // Determinism of the swap rule: with rand just under the ratio the
    // heavier second sample is selected; just above it is kept.
    Sample first {};
    first.point = { 1, 0, 0 };
    Sample second {};
    second.point = { 2, 0, 0 };

    Reservoir take {};
    update(take, first, 1.0F, 0.0F);          // weight_sum=1
    update(take, second, 3.0F, 0.70F);        // ratio = 3/4 = 0.75 -> 0.70 swaps
    EXPECT_NEAR(take.selected.point.x, 2.0F, kEps);

    Reservoir keep {};
    update(keep, first, 1.0F, 0.0F);
    update(keep, second, 3.0F, 0.80F);        // 0.80 > 0.75 -> keeps first
    EXPECT_NEAR(keep.selected.point.x, 1.0F, kEps);
}

// ---------------------------------------------------------------------------
// final_weight (RIS unbiased estimator denominator)
// ---------------------------------------------------------------------------

TEST(RestirGi, FinalWeightZeroOnEmpty)
{
    Reservoir r {};
    EXPECT_NEAR(r.final_weight(1.0F), 0.0F, kEps);
}

TEST(RestirGi, FinalWeightZeroOnNonPositiveTargetPdf)
{
    Reservoir r {};
    r.M = 4;
    r.weight_sum = 8.0F;
    EXPECT_NEAR(r.final_weight(0.0F), 0.0F, kEps);
    EXPECT_NEAR(r.final_weight(-1.0F), 0.0F, kEps);
}

TEST(RestirGi, FinalWeightIsWeightSumOverMTimesTargetPdf)
{
    // Ouyang/Bitterli Eq.: W = weight_sum / (M * p_hat).
    Reservoir r {};
    r.M = 4;
    r.weight_sum = 8.0F;
    EXPECT_NEAR(r.final_weight(2.0F), 8.0F / (4.0F * 2.0F), kEps);  // = 1.0
}

// ---------------------------------------------------------------------------
// History clamp
// ---------------------------------------------------------------------------

TEST(RestirGi, ClampHistoryScalesProportionally)
{
    Reservoir r {};
    r.M = 400;
    r.weight_sum = 800.0F;
    clamp_history(r, 100);
    EXPECT_EQ(r.M, 100U);
    EXPECT_NEAR(r.weight_sum, 200.0F, kEps);  // 800 * 100/400
}

TEST(RestirGi, ClampHistoryNoOpBelowCap)
{
    // Below the cap the reservoir is untouched (no spurious scaling).
    Reservoir r {};
    r.M = 50;
    r.weight_sum = 123.0F;
    clamp_history(r, 100);
    EXPECT_EQ(r.M, 50U);
    EXPECT_NEAR(r.weight_sum, 123.0F, kEps);
}

TEST(RestirGi, ClampHistoryPreservesFinalWeightRatio)
{
    // The whole point of the proportional clamp: final_weight is invariant
    // under the cap (weight_sum and M scale by the same factor).
    Reservoir r {};
    r.M = 600;
    r.weight_sum = 300.0F;
    const float before = r.final_weight(1.5F);
    clamp_history(r, 20);
    const float after = r.final_weight(1.5F);
    EXPECT_NEAR(before, after, kEps);
    EXPECT_EQ(r.M, 20U);
}

// ---------------------------------------------------------------------------
// Spatial / temporal combine (cross-pixel RIS reuse)
// ---------------------------------------------------------------------------

TEST(RestirGi, CombineIgnoresEmptyDonor)
{
    Reservoir dst {};
    update(dst, Sample {}, 1.0F, 0.0F);
    const Reservoir empty {};
    combine(dst, empty, 1.0F, 0.5F, [](const Sample&) { return 5.0F; });
    EXPECT_EQ(dst.M, 1U);  // donor M==0 -> early return, no merge
}

TEST(RestirGi, CombineGrowsMByDonorM)
{
    Reservoir dst {};
    update(dst, Sample {}, 1.0F, 0.5F);
    Reservoir donor {};
    update(donor, Sample {}, 1.0F, 0.5F);
    update(donor, Sample {}, 1.0F, 0.5F);   // donor.M == 2
    combine(dst, donor, 1.0F, 0.5F, [](const Sample&) { return 1.0F; });
    EXPECT_EQ(dst.M, 3U);  // 1 + 2
}

TEST(RestirGi, CombineSelectsDonorAndInvalidatesVisibility)
{
    // Force the swap (huge p_hat + rand just under 1) and verify the donor
    // survivor's visibility is flipped to 0 — Ouyang requires a re-test.
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
    EXPECT_EQ(dst.selected.valid, 0U);  // visibility must be re-tested
    EXPECT_EQ(dst.M, 2U);
}

TEST(RestirGi, CombineZeroPHatNeverSwaps)
{
    // A donor whose target PDF re-evaluates to 0 at the receiver adds 0
    // weight and can never replace the survivor — but still grows M.
    Reservoir dst {};
    Sample keep {};
    keep.point = { 3, 3, 3 };
    update(dst, keep, 1.0F, 0.0F);

    Reservoir donor {};
    update(donor, Sample {}, 1.0F, 0.0F);

    combine(dst, donor, 0.999F, 0.0F, [](const Sample&) { return 0.0F; });
    EXPECT_NEAR(dst.selected.point.x, 3.0F, kEps);  // unchanged
    EXPECT_EQ(dst.M, 2U);
}

// ---------------------------------------------------------------------------
// Temporal blend + age
// ---------------------------------------------------------------------------

TEST(RestirGi, TemporalBlendAlphaZeroIsPureCurrent)
{
    Reservoir cur {};
    cur.weight_sum = 10.0F;
    cur.M = 5;
    cur.selected.point = { 1, 0, 0 };
    Reservoir prev {};
    prev.weight_sum = 99.0F;
    prev.M = 50;
    prev.selected.point = { 9, 0, 0 };

    const Reservoir r = temporal_blend(cur, prev, 0.0F);
    EXPECT_NEAR(r.weight_sum, 10.0F, kEps);
    EXPECT_EQ(r.M, 5U);
    EXPECT_NEAR(r.selected.point.x, 1.0F, kEps);  // alpha<=0.5 -> current
}

TEST(RestirGi, TemporalBlendAlphaOneIsPurePrevious)
{
    Reservoir cur {};
    cur.weight_sum = 10.0F;
    cur.M = 5;
    cur.selected.point = { 1, 0, 0 };
    Reservoir prev {};
    prev.weight_sum = 99.0F;
    prev.M = 50;
    prev.selected.point = { 9, 0, 0 };

    const Reservoir r = temporal_blend(cur, prev, 1.0F);
    EXPECT_NEAR(r.weight_sum, 99.0F, kEps);
    EXPECT_EQ(r.M, 50U);
    EXPECT_NEAR(r.selected.point.x, 9.0F, kEps);  // alpha>0.5 -> previous
}

TEST(RestirGi, TemporalBlendMidpointLerpsWeightAndM)
{
    Reservoir cur {};
    cur.weight_sum = 10.0F;
    cur.M = 4;
    Reservoir prev {};
    prev.weight_sum = 30.0F;
    prev.M = 8;

    const Reservoir r = temporal_blend(cur, prev, 0.5F);
    EXPECT_NEAR(r.weight_sum, 20.0F, kEps);  // 0.5*10 + 0.5*30
    EXPECT_EQ(r.M, 6U);                       // 0.5*4 + 0.5*8
}

TEST(RestirGi, TemporalBlendCarriesCurrentAge)
{
    Reservoir cur {};
    cur.age = 7;
    Reservoir prev {};
    prev.age = 99;
    const Reservoir r = temporal_blend(cur, prev, 0.5F);
    EXPECT_EQ(r.age, 7U);  // result inherits the current reservoir's age
}

TEST(RestirGi, InvalidateResetsToZeroState)
{
    Reservoir r {};
    r.M = 42;
    r.weight_sum = 17.0F;
    r.age = 9;
    r.selected.point = { 5, 5, 5 };
    r.invalidate();
    EXPECT_EQ(r.M, 0U);
    EXPECT_NEAR(r.weight_sum, 0.0F, kEps);
    EXPECT_EQ(r.age, 0U);
    EXPECT_NEAR(r.final_weight(1.0F), 0.0F, kEps);
}

// ---------------------------------------------------------------------------
// GLSL helper exposure (host/device parity surface)
// ---------------------------------------------------------------------------

TEST(RestirGi, GlslHelpersExposeGiReservoirFns)
{
    EXPECT_FALSE(cd::restir_gi::kGiReservoirGlsl.empty());
    EXPECT_NE(cd::restir_gi::kGiReservoirGlsl.find("gi_reservoir_update"),
              std::string_view::npos);
    EXPECT_NE(cd::restir_gi::kGiReservoirGlsl.find("gi_reservoir_final_weight"),
              std::string_view::npos);
}

TEST(RestirGi, AllThreeComputeKernelsArePresent)
{
    // The reservoir-math-v1 ships three CS strings (sample / temporal /
    // spatial). Lock their presence so a refactor cannot silently drop one.
    EXPECT_FALSE(cd::restir_gi::kRestirGiSampleCS.empty());
    EXPECT_FALSE(cd::restir_gi::kRestirGiTemporalReuseCS.empty());
    EXPECT_FALSE(cd::restir_gi::kRestirGiSpatialReuseCS.empty());
    EXPECT_NE(cd::restir_gi::kRestirGiSampleCS.find("local_size_x"),
              std::string_view::npos);
}

TEST(RestirGi, SampleKernelDocumentsDeferredTrace)
{
    // Honest-scope guard: the sample kernel must keep the DEFERRED-TRACE
    // marker so the "GI is not actually traced" contract stays visible to
    // anyone reading the embedded shader.
    EXPECT_NE(cd::restir_gi::kRestirGiSampleCS.find("DEFERRED TRACE"),
              std::string_view::npos);
}

}  // namespace
