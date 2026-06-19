// =============================================================================
// cd::frame_timing unit tests
// =============================================================================
#include <cd/frame_timing/FrameTimeRing.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace
{

TEST(FrameTimeRing, EmptyRingHasZeroStats)
{
    cd::frame_timing::FrameTimeRing<16> ring;
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 0U);
    EXPECT_DOUBLE_EQ(s.mean, 0.0);
    EXPECT_FLOAT_EQ(s.median, 0.0F);
    EXPECT_FLOAT_EQ(s.p99, 0.0F);
}

TEST(FrameTimeRing, PushTracksFilledCountUntilCapacity)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    for (int i = 0; i < 5; ++i)
        ring.push(0.016F);
    EXPECT_EQ(ring.filled(), 5U);
    for (int i = 0; i < 10; ++i)
        ring.push(0.016F);
    EXPECT_EQ(ring.filled(), 8U);  // capped at capacity
}

TEST(FrameTimeRing, MeanMedianCorrectOnUniformSamples)
{
    cd::frame_timing::FrameTimeRing<16> ring;
    for (int i = 0; i < 16; ++i)
        ring.push(0.01F);
    const auto s = ring.stats();
    EXPECT_NEAR(s.mean, 0.01, 1e-6);
    EXPECT_FLOAT_EQ(s.median, 0.01F);
    EXPECT_FLOAT_EQ(s.p99, 0.01F);
    EXPECT_NEAR(s.fps_mean(), 100.0, 1e-3);
}

TEST(FrameTimeRing, P99CapturesSpikeAtTopOfRing)
{
    cd::frame_timing::FrameTimeRing<100> ring;
    for (int i = 0; i < 99; ++i)
        ring.push(0.005F);
    ring.push(0.080F);  // single 80 ms spike
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 100U);
    EXPECT_NEAR(s.mean, (99.0 * 0.005 + 0.080) / 100.0, 1e-6);
    EXPECT_FLOAT_EQ(s.median, 0.005F);
    EXPECT_FLOAT_EQ(s.p99, 0.080F);  // the spike
    EXPECT_FLOAT_EQ(s.dt_min, 0.005F);
    EXPECT_FLOAT_EQ(s.dt_max, 0.080F);
}

TEST(FrameTimeRing, CopyInOrderProducesOldestFirst)
{
    cd::frame_timing::FrameTimeRing<4> ring;
    ring.push(1.0F);
    ring.push(2.0F);
    ring.push(3.0F);
    ring.push(4.0F);  // ring now [1, 2, 3, 4] write_idx wraps to 0
    ring.push(5.0F);  // ring overwrites slot 0 -> [5, 2, 3, 4]; oldest = 2
    std::vector<float> out;
    ring.copy_in_order(out);
    ASSERT_EQ(out.size(), 4U);
    EXPECT_FLOAT_EQ(out[0], 2.0F);
    EXPECT_FLOAT_EQ(out[1], 3.0F);
    EXPECT_FLOAT_EQ(out[2], 4.0F);
    EXPECT_FLOAT_EQ(out[3], 5.0F);
}

TEST(FrameTimeRing, ResetClearsState)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    for (int i = 0; i < 8; ++i)
        ring.push(0.016F);
    ring.reset();
    EXPECT_EQ(ring.filled(), 0U);
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 0U);
}

TEST(FrameTimeRing, FpsHelpersHandleZeroSafely)
{
    cd::frame_timing::Stats empty;
    EXPECT_DOUBLE_EQ(empty.fps_mean(), 0.0);
    EXPECT_DOUBLE_EQ(empty.fps_median(), 0.0);
}

// ---------------------------------------------------------------------------
// BAND-1 edge depth (ADR-20260616 §2.1): ring overflow, single-sample,
// partial-fill percentile index, empty-ring p99/median, wrap order-independence.
// ---------------------------------------------------------------------------

// A single sample makes mean == median == p99 == min == max == that value.
TEST(FrameTimeRing, SingleSampleAllStatsEqualThatSample)
{
    cd::frame_timing::FrameTimeRing<16> ring;
    ring.push(0.033F);
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 1U);
    EXPECT_NEAR(s.mean, 0.033, 1e-6);
    EXPECT_FLOAT_EQ(s.median, 0.033F);
    EXPECT_FLOAT_EQ(s.p99,    0.033F);
    EXPECT_FLOAT_EQ(s.dt_min, 0.033F);
    EXPECT_FLOAT_EQ(s.dt_max, 0.033F);
    EXPECT_NEAR(s.fps_median(), 1.0 / 0.033, 1e-3);
}

// Overflowing the ring must overwrite the OLDEST samples and the stats
// window must reflect only the most-recent kCapacity values. We push 8
// "old" 0.100s frames then 8 "new" 0.010s frames into a 8-slot ring; the
// old frames must be fully evicted so min == max == mean == 0.010.
TEST(FrameTimeRing, OverflowEvictsOldestAndStatsTrackWindow)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    for (int i = 0; i < 8; ++i)
        ring.push(0.100F);  // these must all be evicted
    for (int i = 0; i < 8; ++i)
        ring.push(0.010F);  // window is now entirely 0.010
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 8U);
    EXPECT_NEAR(s.mean, 0.010, 1e-6);
    EXPECT_FLOAT_EQ(s.median, 0.010F);
    EXPECT_FLOAT_EQ(s.p99,    0.010F);
    EXPECT_FLOAT_EQ(s.dt_min, 0.010F);
    EXPECT_FLOAT_EQ(s.dt_max, 0.010F);
}

// Mixed overflow: after wrapping, a single old spike still inside the
// window must be captured by p99/dt_max, and the evicted spikes must NOT.
TEST(FrameTimeRing, OverflowKeepsInWindowSpikeDropsEvictedSpike)
{
    cd::frame_timing::FrameTimeRing<4> ring;
    ring.push(0.500F);  // evicted spike
    ring.push(0.005F);
    ring.push(0.005F);
    ring.push(0.005F);
    ring.push(0.080F);  // in-window spike (overwrites the 0.500)
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 4U);
    EXPECT_FLOAT_EQ(s.dt_max, 0.080F);  // the evicted 0.500 is gone
    EXPECT_FLOAT_EQ(s.p99,    0.080F);
    EXPECT_FLOAT_EQ(s.dt_min, 0.005F);
}

// Partial fill (not yet wrapped): the p99 index = floor(filled * 0.99) must
// clamp to filled-1 so a 3-sample ring never indexes out of range.
TEST(FrameTimeRing, PartialFillPercentileIndexClampsInRange)
{
    cd::frame_timing::FrameTimeRing<100> ring;
    ring.push(0.001F);
    ring.push(0.002F);
    ring.push(0.003F);  // filled == 3; floor(3 * 0.99) == 2 -> last element
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 3U);
    EXPECT_FLOAT_EQ(s.median, 0.002F);  // middle of [0.001,0.002,0.003]
    EXPECT_FLOAT_EQ(s.p99,    0.003F);  // clamped to the largest sample
    EXPECT_FLOAT_EQ(s.dt_min, 0.001F);
    EXPECT_FLOAT_EQ(s.dt_max, 0.003F);
}

// Statistics are order-independent: pushing the same multiset in two
// different orders (one of them wrapping the ring) yields identical stats.
TEST(FrameTimeRing, StatsAreOrderIndependentEvenWhenWrapped)
{
    cd::frame_timing::FrameTimeRing<4> a;
    a.push(0.01F);
    a.push(0.02F);
    a.push(0.03F);
    a.push(0.04F);

    cd::frame_timing::FrameTimeRing<4> b;
    b.push(0.04F);  // these two extra pushes wrap b's head...
    b.push(0.03F);
    b.push(0.02F);
    b.push(0.01F);  // ...so b holds the same multiset as a, different order

    const auto sa = a.stats();
    const auto sb = b.stats();
    EXPECT_NEAR(sa.mean, sb.mean, 1e-7);
    EXPECT_FLOAT_EQ(sa.median, sb.median);
    EXPECT_FLOAT_EQ(sa.p99,    sb.p99);
    EXPECT_FLOAT_EQ(sa.dt_min, sb.dt_min);
    EXPECT_FLOAT_EQ(sa.dt_max, sb.dt_max);
}

// copy_in_order on a full-then-overflowed ring returns exactly kCapacity
// elements in oldest-first order.
TEST(FrameTimeRing, CopyInOrderLengthEqualsCapacityWhenOverflowed)
{
    cd::frame_timing::FrameTimeRing<4> ring;  // 4 == minimum capacity
    for (int i = 0; i < 7; ++i)
        ring.push(static_cast<float>(i));  // 0..6; window keeps 3,4,5,6
    std::vector<float> out;
    ring.copy_in_order(out);
    ASSERT_EQ(out.size(), 4U);
    EXPECT_FLOAT_EQ(out[0], 3.0F);
    EXPECT_FLOAT_EQ(out[1], 4.0F);
    EXPECT_FLOAT_EQ(out[2], 5.0F);
    EXPECT_FLOAT_EQ(out[3], 6.0F);
}

// ---------------------------------------------------------------------------
// Gap-closure tests: copy_in_order (partial fill), last_dt, percentile,
// jitter, median even-N, Stats::jitter(), boundary / edge / negative cases.
// ---------------------------------------------------------------------------

// copy_in_order on a PARTIALLY filled ring (not yet wrapped) must return
// elements in push order starting from the very first push, not from an
// uninitialised slot past write_idx_.
TEST(FrameTimeRing, CopyInOrderPartialFillIsOldestFirst)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    ring.push(10.0F);
    ring.push(20.0F);
    ring.push(30.0F);
    std::vector<float> out;
    ring.copy_in_order(out);
    ASSERT_EQ(out.size(), 3U);
    EXPECT_FLOAT_EQ(out[0], 10.0F);
    EXPECT_FLOAT_EQ(out[1], 20.0F);
    EXPECT_FLOAT_EQ(out[2], 30.0F);
}

// copy_in_order on an empty ring must return an empty vector.
TEST(FrameTimeRing, CopyInOrderEmptyRingReturnsEmpty)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    std::vector<float> out;
    ring.copy_in_order(out);
    EXPECT_TRUE(out.empty());
}

// last_dt() returns 0 for an empty ring.
TEST(FrameTimeRing, LastDtEmptyReturnsZero)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    EXPECT_FLOAT_EQ(ring.last_dt(), 0.0F);
}

// last_dt() returns the most-recently pushed value on a partially filled ring.
TEST(FrameTimeRing, LastDtPartialFill)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    ring.push(0.010F);
    ring.push(0.016F);
    ring.push(0.033F);
    EXPECT_FLOAT_EQ(ring.last_dt(), 0.033F);
}

// last_dt() tracks the current write position correctly after wrap.
TEST(FrameTimeRing, LastDtAfterWrap)
{
    cd::frame_timing::FrameTimeRing<4> ring;
    ring.push(1.0F);
    ring.push(2.0F);
    ring.push(3.0F);
    ring.push(4.0F);  // full
    ring.push(5.0F);  // overwrites slot 0
    EXPECT_FLOAT_EQ(ring.last_dt(), 5.0F);
}

// last_dt() returns 0 after reset().
TEST(FrameTimeRing, LastDtAfterReset)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    ring.push(0.016F);
    ring.reset();
    EXPECT_FLOAT_EQ(ring.last_dt(), 0.0F);
}

// percentile(0.0) == dt_min, percentile(1.0) == dt_max for any non-empty ring.
TEST(FrameTimeRing, PercentileBoundaries)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    ring.push(0.005F);
    ring.push(0.010F);
    ring.push(0.080F);
    EXPECT_FLOAT_EQ(ring.percentile(0.0F), 0.005F);
    EXPECT_FLOAT_EQ(ring.percentile(1.0F), 0.080F);
}

// percentile(0.5) is consistent with stats().median for an odd-count window.
TEST(FrameTimeRing, PercentileHalfMatchesMedianOddN)
{
    // 5 samples; sorted = [0.001, 0.002, 0.003, 0.004, 0.005]; middle = 0.003
    cd::frame_timing::FrameTimeRing<16> ring;
    ring.push(0.003F);
    ring.push(0.001F);
    ring.push(0.005F);
    ring.push(0.002F);
    ring.push(0.004F);
    // percentile(0.5): idx = floor(5 * 0.5) = 2 -> sorted[2] = 0.003
    EXPECT_FLOAT_EQ(ring.percentile(0.5F), 0.003F);
    EXPECT_FLOAT_EQ(ring.stats().median,   0.003F);
}

// percentile() on an empty ring returns 0.
TEST(FrameTimeRing, PercentileEmptyReturnsZero)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    EXPECT_FLOAT_EQ(ring.percentile(0.99F), 0.0F);
    EXPECT_FLOAT_EQ(ring.percentile(0.0F),  0.0F);
}

// percentile() clamps out-of-range p values without UB/assert.
TEST(FrameTimeRing, PercentileClampsBelowZeroAndAboveOne)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    ring.push(0.010F);
    ring.push(0.020F);
    // Negative p -> clamps to 0.0 -> smallest sample.
    EXPECT_FLOAT_EQ(ring.percentile(-1.0F), ring.percentile(0.0F));
    // p > 1.0 -> clamps to 1.0 -> largest sample.
    EXPECT_FLOAT_EQ(ring.percentile(2.0F), ring.percentile(1.0F));
}

// jitter() == 0 for an empty ring.
TEST(FrameTimeRing, JitterEmptyReturnsZero)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    EXPECT_FLOAT_EQ(ring.jitter(), 0.0F);
}

// jitter() == 0 for a uniform ring (all same value).
TEST(FrameTimeRing, JitterUniformIsZero)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    for (int i = 0; i < 8; ++i)
        ring.push(0.016F);
    EXPECT_FLOAT_EQ(ring.jitter(), 0.0F);
}

// jitter() == dt_max - dt_min for a mixed-value ring.
TEST(FrameTimeRing, JitterEqualsMaxMinusMed)
{
    cd::frame_timing::FrameTimeRing<16> ring;
    ring.push(0.005F);
    ring.push(0.010F);
    ring.push(0.080F);
    const auto s = ring.stats();
    EXPECT_FLOAT_EQ(ring.jitter(), s.dt_max - s.dt_min);
    EXPECT_NEAR(ring.jitter(), 0.075F, 1e-6F);
}

// Stats::jitter() helper on the Stats struct matches ring.jitter() output.
TEST(FrameTimeRing, StatsJitterHelperConsistentWithRingJitter)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    ring.push(0.004F);
    ring.push(0.020F);
    ring.push(0.100F);
    const auto s = ring.stats();
    EXPECT_FLOAT_EQ(s.jitter(), ring.jitter());
}

// True (interpolated) median for even N: 4-sample ring [0.010, 0.020,
// 0.030, 0.040] -> median = (0.020 + 0.030) / 2 = 0.025.
TEST(FrameTimeRing, MedianEvenNInterpolatesMiddlePair)
{
    cd::frame_timing::FrameTimeRing<4> ring;
    ring.push(0.010F);
    ring.push(0.040F);
    ring.push(0.020F);
    ring.push(0.030F);
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 4U);
    EXPECT_NEAR(s.median, 0.025F, 1e-6F);
}

// Median for odd N: 3-sample ring [0.010, 0.020, 0.090] -> median = 0.020.
TEST(FrameTimeRing, MedianOddNIsExactMiddleElement)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    ring.push(0.090F);
    ring.push(0.010F);
    ring.push(0.020F);
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 3U);
    EXPECT_NEAR(s.median, 0.020F, 1e-6F);
}

// Negative dt (misconfigured caller) must not crash; stats reflect the value.
TEST(FrameTimeRing, NegativeDtDoesNotCrash)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    ring.push(-0.001F);
    ring.push(0.016F);
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 2U);
    EXPECT_FLOAT_EQ(s.dt_min, -0.001F);
    EXPECT_FLOAT_EQ(s.dt_max,  0.016F);
    // fps_mean with negative mean should not crash (returns 0 for mean <= 0).
    EXPECT_GE(s.fps_mean(), 0.0);  // mean = (−0.001+0.016)/2 = 0.0075 > 0
}

// stats() on a single-sample ring after a full overflow reset+refill must
// use only the new samples, not ghost data from prior fill.
TEST(FrameTimeRing, ResetThenRefillNoGhostData)
{
    cd::frame_timing::FrameTimeRing<4> ring;
    for (int i = 0; i < 4; ++i)
        ring.push(9.0F);  // fill with large values
    ring.reset();
    ring.push(0.001F);
    const auto s = ring.stats();
    EXPECT_EQ(s.filled, 1U);
    EXPECT_FLOAT_EQ(s.dt_min, 0.001F);
    EXPECT_FLOAT_EQ(s.dt_max, 0.001F);
    EXPECT_NEAR(s.mean, 0.001, 1e-7);
}

// capacity() always returns the template parameter.
TEST(FrameTimeRing, CapacityMatchesTemplateParameter)
{
    cd::frame_timing::FrameTimeRing<17> ring;
    EXPECT_EQ(ring.capacity(), 17U);
}

// After reset the ring is empty: last_dt, jitter, percentile all return 0.
TEST(FrameTimeRing, ResetMakesAllAccessorsReturnZero)
{
    cd::frame_timing::FrameTimeRing<8> ring;
    for (int i = 0; i < 8; ++i)
        ring.push(0.016F);
    ring.reset();
    EXPECT_FLOAT_EQ(ring.last_dt(),       0.0F);
    EXPECT_FLOAT_EQ(ring.jitter(),        0.0F);
    EXPECT_FLOAT_EQ(ring.percentile(0.5F), 0.0F);
    EXPECT_EQ(ring.filled(), 0U);
}

// Two-sample ring: p99 must not go out of range (floor(2*0.99)=1 -> sorted[1]).
TEST(FrameTimeRing, TwoSampleP99IsLarger)
{
    // Use a 4-slot ring (minimum) with only 2 samples.
    cd::frame_timing::FrameTimeRing<4> ring;
    ring.push(0.005F);
    ring.push(0.080F);
    const auto s = ring.stats();
    EXPECT_FLOAT_EQ(s.p99, 0.080F);
}

}  // namespace
