// =============================================================================
// CHROMODYNAMIC — cd::profile::gpu_marker tests
// Phase 606
//
// Tests validate the API surface and stub counter behavior.
// NullCommandBuffer and NullDevice are used so no GPU is required.
// =============================================================================
#include <cd/profile/gpu_marker/GpuMarker.hpp>

#include <cd/rhi/NullCommandBuffer.hpp>
#include <cd/rhi/NullDevice.hpp>
#include <gtest/gtest.h>

using cd::profile::gpu_marker::MarkerHandle;
using cd::profile::gpu_marker::Recorder;
using cd::profile::gpu_marker::Scope;

// ---------------------------------------------------------------------------
// Test 1: begin_marker + end_marker round-trip
//   * handle returned by begin_marker is valid.
//   * After end_marker + resolve, one sample is visible in samples().
//   * Sample name matches.
//   * duration_ms_computed > 0 (stub counter ticks differ by at least 1).
// ---------------------------------------------------------------------------
TEST(GpuMarker_Recorder, BeginEndRoundTrip)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    EXPECT_TRUE(rec.samples().empty());

    const MarkerHandle h = rec.begin_marker(cmd, "shadow_pass");
    EXPECT_TRUE(h.valid());

    rec.end_marker(cmd, h);

    // Before resolve: resolved bucket is empty (sample sits in pending).
    EXPECT_TRUE(rec.samples().empty());

    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 1U);
    EXPECT_EQ(s[0].name, "shadow_pass");
    // end_tick must be strictly after start_tick.
    EXPECT_GT(s[0].gpu_end_tick, s[0].gpu_start_tick);
    // With stub 1 GHz clock, 1 tick = 1e-6 ms, so duration_ms_computed > 0.
    EXPECT_GT(s[0].duration_ms_computed, 0.0);

    // Debug group bookkeeping: NullCommandBuffer tracks push_debug_group calls.
    EXPECT_FALSE(cmd.log().debug_groups.empty());
    EXPECT_EQ(cmd.log().debug_groups[0], "shadow_pass");
}

// ---------------------------------------------------------------------------
// Test 2: RAII Scope — constructor calls begin_marker, destructor calls
//         end_marker, one sample committed after scope exits.
// ---------------------------------------------------------------------------
TEST(GpuMarker_Scope, RaiiScopeCommitsSample)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    {
        Scope sc(rec, cmd, "geometry_pass");
        // Pending is non-empty only internally — resolved bucket stays empty
        // until resolve().
    }
    // Destructor ran → end_marker called → sample in pending.
    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 1U);
    EXPECT_EQ(s[0].name, "geometry_pass");
    EXPECT_GT(s[0].gpu_end_tick, s[0].gpu_start_tick);
}

// ---------------------------------------------------------------------------
// Test 3: Multiple markers in flight — 3 sequential markers, all resolved.
//   * Tick ordering: each begin_marker/end_marker call advances the counter.
//   * All 3 samples present after resolve().
// ---------------------------------------------------------------------------
TEST(GpuMarker_Recorder, MultipleInFlight)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    const MarkerHandle h0 = rec.begin_marker(cmd, "depth_pre");
    const MarkerHandle h1 = rec.begin_marker(cmd, "lighting");
    const MarkerHandle h2 = rec.begin_marker(cmd, "post_fx");

    rec.end_marker(cmd, h0);
    rec.end_marker(cmd, h1);
    rec.end_marker(cmd, h2);

    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 3U);

    // Names must match insertion order.
    EXPECT_EQ(s[0].name, "depth_pre");
    EXPECT_EQ(s[1].name, "lighting");
    EXPECT_EQ(s[2].name, "post_fx");

    // Each sample must have end_tick > start_tick.
    for (const auto& sample : s)
    {
        EXPECT_GT(sample.gpu_end_tick, sample.gpu_start_tick)
            << "Failed for sample: " << sample.name;
        EXPECT_GT(sample.duration_ms_computed, 0.0)
            << "Zero duration for sample: " << sample.name;
    }
}

// ---------------------------------------------------------------------------
// Test 4: clear() empties resolved and resets the counter.
//   * After clear(), samples() is empty.
//   * New markers recorded after clear() start from tick 0 again.
// ---------------------------------------------------------------------------
TEST(GpuMarker_Recorder, ClearEmptiesSamples)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    {
        const MarkerHandle h = rec.begin_marker(cmd, "before_clear");
        rec.end_marker(cmd, h);
    }
    rec.resolve(dev);
    ASSERT_EQ(rec.samples().size(), 1U);

    rec.clear();
    EXPECT_TRUE(rec.samples().empty());

    // After clear, new markers still work correctly.
    const MarkerHandle h2 = rec.begin_marker(cmd, "after_clear");
    rec.end_marker(cmd, h2);
    rec.resolve(dev);

    const auto s = rec.samples();
    ASSERT_EQ(s.size(), 1U);
    EXPECT_EQ(s[0].name, "after_clear");
    // Counter reset: start_tick should be 0 (first begin after clear).
    EXPECT_EQ(s[0].gpu_start_tick, 0U);
}

// ---------------------------------------------------------------------------
// Test 5: end_marker with invalid handle is a no-op (no crash, no sample).
// ---------------------------------------------------------------------------
TEST(GpuMarker_Recorder, InvalidHandleNoOp)
{
    cd::rhi::NullCommandBuffer cmd;
    cd::rhi::NullDevice        dev;
    Recorder                   rec;

    const MarkerHandle bad {};
    EXPECT_FALSE(bad.valid());

    // Must not crash.
    rec.end_marker(cmd, bad);
    rec.resolve(dev);
    EXPECT_TRUE(rec.samples().empty());
}
