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

}  // namespace
