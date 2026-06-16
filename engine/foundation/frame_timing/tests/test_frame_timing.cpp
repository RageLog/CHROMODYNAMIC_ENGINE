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

}  // namespace
