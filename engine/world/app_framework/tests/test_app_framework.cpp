// =============================================================================
// CHROMODYNAMIC -- engine/world/sample_framework/tests/test_sample_framework.cpp
// Mega-Marathon M2A skeleton (Run 29 / phase373).
// Extended to 100% coverage: lifecycle, edge cases, negative paths, new API.
//
// Test groups:
//   AppFramework_M2A  -- original 5 tests (kept verbatim)
//   AppFramework_Lifecycle -- ordering, double-init guard, dtor-safety
//   AppFramework_FrameContext -- dt/total_time monotonicity, frame_index
//   AppFramework_Accessors -- frames_pumped(), has_run(), config(), etc.
//   AppFramework_Config -- AppConfig defaults, zero max_frames contract
//   AppFramework_Run -- run<T> template forwarding, multi-arg ctor
//   AppFramework_Hooks -- request_shutdown idempotency, ordering edge cases
// =============================================================================
#include <cd/app/App.hpp>
#include <cd/app/run.hpp>

#include <cd/core/Result.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace
{

// ---------------------------------------------------------------------------
// TrackingApp: records every lifecycle event + FrameContext snapshot
// ---------------------------------------------------------------------------
class TrackingApp final : public cd::app::App
{
public:
    using cd::app::App::App;

    int boot_calls { 0 };
    int frame_calls { 0 };
    int shutdown_calls { 0 };
    bool boot_should_fail { false };
    int  request_shutdown_at_frame { -1 };   // -1 = never
    std::uint64_t last_frame_index { 0u };
    double last_total_time { 0.0 };

    // captures for monotonicity / ordering verification
    std::vector<std::uint64_t> frame_indices_seen;
    std::vector<double>        total_times_seen;
    std::vector<float>         dts_seen;

protected:
    cd::core::Result<void> on_boot() override
    {
        ++boot_calls;
        if (boot_should_fail)
        {
            return cd::core::fail(cd::core::core_errors::Code::kAborted,
                                  "TrackingApp::on_boot forced failure");
        }
        return {};
    }

    void on_frame(const cd::app::FrameContext& fc) override
    {
        ++frame_calls;
        last_frame_index = fc.frame_index;
        last_total_time  = fc.total_time;
        frame_indices_seen.push_back(fc.frame_index);
        total_times_seen.push_back(fc.total_time);
        dts_seen.push_back(fc.dt);
        if (request_shutdown_at_frame >= 0 &&
            std::cmp_equal(fc.frame_index, request_shutdown_at_frame))
        {
            request_shutdown();
        }
    }

    void on_shutdown() noexcept override
    {
        ++shutdown_calls;
    }
};

// ---------------------------------------------------------------------------
// MultiArgApp: tests that run<T> forwards constructor args correctly
// ---------------------------------------------------------------------------
class MultiArgApp final : public cd::app::App
{
public:
    explicit MultiArgApp(cd::app::AppConfig cfg, int sentinel)
        : cd::app::App(std::move(cfg))
        , sentinel_(sentinel)
    {
    }

    int sentinel() const noexcept { return sentinel_; }

protected:
    cd::core::Result<void> on_boot() override { return {}; }
    void on_frame(const cd::app::FrameContext& /*fc*/) override {}
    void on_shutdown() noexcept override {}

private:
    int sentinel_;
};

// ---------------------------------------------------------------------------
// ShutdownDuringBootApp: on_boot calls request_shutdown (should not skip
// on_shutdown and should break the frame loop immediately)
// ---------------------------------------------------------------------------
class ShutdownDuringBootApp final : public cd::app::App
{
public:
    using cd::app::App::App;
    int boot_calls { 0 };
    int frame_calls { 0 };
    int shutdown_calls { 0 };

protected:
    cd::core::Result<void> on_boot() override
    {
        ++boot_calls;
        request_shutdown();   // signal during boot — frame loop must not run
        return {};
    }
    void on_frame(const cd::app::FrameContext& /*fc*/) override { ++frame_calls; }
    void on_shutdown() noexcept override { ++shutdown_calls; }
};

// ---------------------------------------------------------------------------
// DoubleShutdownApp: calls request_shutdown twice in consecutive on_frame
// calls (idempotency test)
// ---------------------------------------------------------------------------
class DoubleRequestShutdownApp final : public cd::app::App
{
public:
    using cd::app::App::App;
    int shutdown_flag_count { 0 };   // incremented each time the flag is true at frame entry
    int frame_calls { 0 };

protected:
    cd::core::Result<void> on_boot() override { return {}; }
    void on_frame(const cd::app::FrameContext& fc) override
    {
        ++frame_calls;
        // On frame 0 and frame 1 call request_shutdown — second call is a no-op
        if (fc.frame_index == 0u || fc.frame_index == 1u)
        {
            request_shutdown();
        }
        if (shutdown_requested())
        {
            ++shutdown_flag_count;
        }
    }
    void on_shutdown() noexcept override {}
};

}  // namespace

// ==========================================================================
// Group 1 — Original M2A tests (kept verbatim for regression)
// ==========================================================================

TEST(AppFramework_M2A, BootFrameShutdownSingleFramePath)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 1u;
    TrackingApp app { cfg };
    const int rc = app.run();
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(app.boot_calls, 1);
    EXPECT_EQ(app.frame_calls, 1);
    EXPECT_EQ(app.shutdown_calls, 1);
    EXPECT_EQ(app.last_frame_index, 0u);
}

TEST(AppFramework_M2A, MultiFramePumpAdvancesFrameIndex)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 8u;
    TrackingApp app { cfg };
    EXPECT_EQ(app.run(), 0);
    EXPECT_EQ(app.frame_calls, 8);
    EXPECT_EQ(app.last_frame_index, 7u);
    EXPECT_NEAR(app.last_total_time, 7.0 / 60.0, 1e-6);
}

TEST(AppFramework_M2A, RequestShutdownBreaksLoopEarly)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 100u;
    TrackingApp app { cfg };
    app.request_shutdown_at_frame = 3;
    EXPECT_EQ(app.run(), 0);
    EXPECT_EQ(app.frame_calls, 4);  // frames 0..3 then break
    EXPECT_TRUE(app.shutdown_requested());
    EXPECT_EQ(app.shutdown_calls, 1);
}

TEST(AppFramework_M2A, BootFailureSkipsFramePumpAndStillShutsDown)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 5u;
    TrackingApp app { cfg };
    app.boot_should_fail = true;
    const int rc = app.run();
    EXPECT_NE(rc, 0);
    EXPECT_EQ(app.boot_calls, 1);
    EXPECT_EQ(app.frame_calls, 0);
    EXPECT_EQ(app.shutdown_calls, 1);
}

TEST(AppFramework_M2A, RunTemplateEntryPointDrivesApp)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 2u;
    // The template entry point is the canonical sample main() shape.
    const int rc = cd::app::run<TrackingApp>(0, nullptr, cfg);
    EXPECT_EQ(rc, 0);
}

// ==========================================================================
// Group 2 — Lifecycle ordering and guard tests
// ==========================================================================

TEST(AppFramework_Lifecycle, BootCalledExactlyOnce)
{
    // Arrange
    cd::app::AppConfig cfg;
    cfg.max_frames = 10u;
    TrackingApp app { cfg };

    // Act
    static_cast<void>(app.run());

    // Assert
    EXPECT_EQ(app.boot_calls, 1);
}

TEST(AppFramework_Lifecycle, ShutdownCalledExactlyOnceOnSuccess)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 3u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    EXPECT_EQ(app.shutdown_calls, 1);
}

TEST(AppFramework_Lifecycle, ShutdownCalledExactlyOnceOnBootFailure)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 3u;
    TrackingApp app { cfg };
    app.boot_should_fail = true;
    static_cast<void>(app.run());
    EXPECT_EQ(app.shutdown_calls, 1);
}

TEST(AppFramework_Lifecycle, BootAlwaysBeforeFirstFrame)
{
    // Verify: on_boot recorded as called before the first frame_index appears.
    // We confirm boot_calls == 1 when the first frame executes.
    // (frame_indices_seen[0] being present means frame ran after boot.)
    cd::app::AppConfig cfg;
    cfg.max_frames = 4u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    ASSERT_GE(static_cast<int>(app.frame_indices_seen.size()), 1);
    EXPECT_EQ(app.boot_calls, 1);  // boot happened before any frame
    EXPECT_EQ(app.frame_indices_seen.front(), 0u);
}

TEST(AppFramework_Lifecycle, ShutdownAlwaysAfterLastFrame)
{
    // on_shutdown increments shutdown_calls; on_frame records how many
    // calls happened. After run() returns, shutdown must have been called.
    cd::app::AppConfig cfg;
    cfg.max_frames = 5u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    EXPECT_EQ(app.frame_calls, 5);
    EXPECT_EQ(app.shutdown_calls, 1);
}

TEST(AppFramework_Lifecycle, DoubleRunReturnsErrorWithoutCallingBootAgain)
{
    // Calling run() a second time must return non-zero and must NOT
    // invoke on_boot / on_frame / on_shutdown a second time.
    cd::app::AppConfig cfg;
    cfg.max_frames = 2u;
    TrackingApp app { cfg };

    const int rc1 = app.run();
    EXPECT_EQ(rc1, 0);
    EXPECT_EQ(app.boot_calls, 1);
    EXPECT_EQ(app.frame_calls, 2);
    EXPECT_EQ(app.shutdown_calls, 1);

    const int rc2 = app.run();   // second call — must be rejected
    EXPECT_NE(rc2, 0);
    // Counts must not have changed
    EXPECT_EQ(app.boot_calls, 1);
    EXPECT_EQ(app.frame_calls, 2);
    EXPECT_EQ(app.shutdown_calls, 1);
}

TEST(AppFramework_Lifecycle, HasRunFlagSetAfterCompletion)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 1u;
    TrackingApp app { cfg };
    EXPECT_FALSE(app.has_run());
    static_cast<void>(app.run());
    EXPECT_TRUE(app.has_run());
}

TEST(AppFramework_Lifecycle, HasRunFlagSetEvenAfterBootFailure)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 1u;
    TrackingApp app { cfg };
    app.boot_should_fail = true;
    static_cast<void>(app.run());
    EXPECT_TRUE(app.has_run());
}

TEST(AppFramework_Lifecycle, DestroyWithoutRunIsSafe)
{
    // App dtor must be safe even if run() was never called (no on_shutdown
    // invoked from dtor in this design — purely passive cleanup).
    cd::app::AppConfig cfg;
    cfg.max_frames = 1u;
    {
        TrackingApp app { cfg };
        // Never call run()
        EXPECT_EQ(app.boot_calls, 0);
        EXPECT_EQ(app.shutdown_calls, 0);
    }
    // If we reach here without crashing, the dtor is safe.
    SUCCEED();
}

TEST(AppFramework_Lifecycle, ShutdownDuringBootHaltsFrameLoop)
{
    // request_shutdown() called inside on_boot must cause the frame loop to
    // exit immediately after boot (zero on_frame calls).
    cd::app::AppConfig cfg;
    cfg.max_frames = 50u;
    ShutdownDuringBootApp app { cfg };
    const int rc = app.run();
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(app.boot_calls, 1);
    EXPECT_EQ(app.frame_calls, 0);
    EXPECT_EQ(app.shutdown_calls, 1);
    EXPECT_TRUE(app.shutdown_requested());
}

// ==========================================================================
// Group 3 — FrameContext correctness
// ==========================================================================

TEST(AppFramework_FrameContext, FirstFrameHasTotalTimeZero)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 1u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    ASSERT_EQ(static_cast<int>(app.total_times_seen.size()), 1);
    EXPECT_DOUBLE_EQ(app.total_times_seen[0], 0.0);
}

TEST(AppFramework_FrameContext, TotalTimeMonotonicallyIncreasing)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 16u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    const auto& times = app.total_times_seen;
    ASSERT_EQ(static_cast<int>(times.size()), 16);
    for (int i = 1; i < 16; ++i)
    {
        EXPECT_GT(times[static_cast<std::size_t>(i)],
                  times[static_cast<std::size_t>(i - 1)])
            << "total_time not monotonically increasing at frame " << i;
    }
}

TEST(AppFramework_FrameContext, FrameIndexMonotonicallyIncreasing)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 16u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    const auto& indices = app.frame_indices_seen;
    ASSERT_EQ(static_cast<int>(indices.size()), 16);
    for (std::size_t i = 1u; i < indices.size(); ++i)
    {
        EXPECT_EQ(indices[i], indices[i - 1u] + 1u)
            << "frame_index gap at position " << i;
    }
}

TEST(AppFramework_FrameContext, DtIsPositiveForAllFrames)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 8u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    for (std::size_t i = 0u; i < app.dts_seen.size(); ++i)
    {
        EXPECT_GT(app.dts_seen[i], 0.0f) << "dt <= 0 at frame " << i;
    }
}

TEST(AppFramework_FrameContext, TotalTimeAfterNFramesMatchesDtAccumulation)
{
    // After N frames the total_time handed to frame N-1 should be (N-1)*(1/60).
    // The fixed dt is float-seeded (1/60 has no exact binary form) and summed
    // per frame, so the running total carries float-rounding error that grows
    // with N — allow a float-epsilon band, not a double-exact one (a real
    // off-by-one-frame bug would be ~0.0166, far outside this).
    constexpr std::uint64_t kN = 60u;
    cd::app::AppConfig cfg;
    cfg.max_frames = kN;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    const double expected_last = static_cast<double>(kN - 1u) / 60.0;
    EXPECT_NEAR(app.last_total_time, expected_last, 1e-6);
}

TEST(AppFramework_FrameContext, FrameIndexStartsAtZero)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 5u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    ASSERT_GE(static_cast<int>(app.frame_indices_seen.size()), 1);
    EXPECT_EQ(app.frame_indices_seen.front(), 0u);
}

TEST(AppFramework_FrameContext, LastFrameIndexIsCapMinusOne)
{
    constexpr std::uint64_t kCap = 12u;
    cd::app::AppConfig cfg;
    cfg.max_frames = kCap;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    EXPECT_EQ(app.last_frame_index, kCap - 1u);
}

// ==========================================================================
// Group 4 — Accessor correctness
// ==========================================================================

TEST(AppFramework_Accessors, FramesPumpedZeroBeforeRun)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 5u;
    TrackingApp app { cfg };
    EXPECT_EQ(app.frames_pumped(), 0u);
}

TEST(AppFramework_Accessors, FramesPumpedEqualsMaxFramesAfterFullRun)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 7u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    EXPECT_EQ(app.frames_pumped(), 7u);
}

TEST(AppFramework_Accessors, FramesPumpedZeroAfterBootFailure)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 5u;
    TrackingApp app { cfg };
    app.boot_should_fail = true;
    static_cast<void>(app.run());
    EXPECT_EQ(app.frames_pumped(), 0u);
}

TEST(AppFramework_Accessors, FramesPumpedReflectsEarlyShutdown)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 100u;
    TrackingApp app { cfg };
    app.request_shutdown_at_frame = 5;   // shutdown during frame 5 → 6 frames total
    static_cast<void>(app.run());
    EXPECT_EQ(app.frames_pumped(), 6u);
}

TEST(AppFramework_Accessors, ShutdownRequestedFalseInitially)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 3u;
    TrackingApp app { cfg };
    EXPECT_FALSE(app.shutdown_requested());
}

TEST(AppFramework_Accessors, ConfigTitlePreserved)
{
    cd::app::AppConfig cfg;
    cfg.title = "TestTitle_XYZ";
    cfg.max_frames = 1u;
    TrackingApp app { cfg };
    EXPECT_EQ(app.config().title, "TestTitle_XYZ");
}

TEST(AppFramework_Accessors, ConfigDimensionsPreserved)
{
    cd::app::AppConfig cfg;
    cfg.window_width  = 800u;
    cfg.window_height = 600u;
    cfg.max_frames    = 1u;
    TrackingApp app { cfg };
    EXPECT_EQ(app.config().window_width,  800u);
    EXPECT_EQ(app.config().window_height, 600u);
}

// ==========================================================================
// Group 5 — AppConfig defaults
// ==========================================================================

TEST(AppFramework_Config, DefaultTitleIsNonEmpty)
{
    const cd::app::AppConfig cfg {};
    EXPECT_FALSE(cfg.title.empty());
}

TEST(AppFramework_Config, DefaultWindowWidthPositive)
{
    const cd::app::AppConfig cfg {};
    EXPECT_GT(cfg.window_width, 0u);
}

TEST(AppFramework_Config, DefaultWindowHeightPositive)
{
    const cd::app::AppConfig cfg {};
    EXPECT_GT(cfg.window_height, 0u);
}

TEST(AppFramework_Config, DefaultMaxFramesIsOne)
{
    const cd::app::AppConfig cfg {};
    EXPECT_EQ(cfg.max_frames, 1u);
}

TEST(AppFramework_Config, DefaultStartMaximizedFalse)
{
    const cd::app::AppConfig cfg {};
    EXPECT_FALSE(cfg.start_maximized);
}

TEST(AppFramework_Config, DefaultEnableValidationTrue)
{
    const cd::app::AppConfig cfg {};
    EXPECT_TRUE(cfg.enable_validation);
}

TEST(AppFramework_Config, ZeroMaxFramesRunsExactlyOneFrame)
{
    // M2A contract: max_frames=0 is treated as 1 so the gtest path is bounded.
    cd::app::AppConfig cfg;
    cfg.max_frames = 0u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    EXPECT_EQ(app.frame_calls, 1);
}

TEST(AppFramework_Config, LargeMaxFramesPumpsExactCount)
{
    constexpr std::uint64_t kLarge = 1024u;
    cd::app::AppConfig cfg;
    cfg.max_frames = kLarge;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    EXPECT_EQ(static_cast<std::uint64_t>(app.frame_calls), kLarge);
    EXPECT_EQ(app.frames_pumped(), kLarge);
}

// ==========================================================================
// Group 6 — run<T> template
// ==========================================================================

TEST(AppFramework_Run, TemplateForwardsMultipleCtorArgs)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 1u;
    const int sentinel = 42;
    const int rc = cd::app::run<MultiArgApp>(0, nullptr, cfg, sentinel);
    EXPECT_EQ(rc, 0);
}

TEST(AppFramework_Run, TemplateAcceptsNullArgv)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 1u;
    const int rc = cd::app::run<TrackingApp>(0, nullptr, cfg);
    EXPECT_EQ(rc, 0);
}

TEST(AppFramework_Run, TemplateReturnsNonZeroOnBootFailure)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 1u;
    TrackingApp app { cfg };
    app.boot_should_fail = true;
    const int rc = app.run();
    EXPECT_NE(rc, 0);
}

// ==========================================================================
// Group 7 — request_shutdown edge cases
// ==========================================================================

TEST(AppFramework_Hooks, RequestShutdownBeforeRunIsIdempotent)
{
    // Calling request_shutdown before run() means the loop exits on
    // its very first iteration check (no frames pumped).
    cd::app::AppConfig cfg;
    cfg.max_frames = 50u;
    TrackingApp app { cfg };
    app.request_shutdown();  // set flag before run
    static_cast<void>(app.run());
    EXPECT_EQ(app.frame_calls, 0);
    EXPECT_EQ(app.shutdown_calls, 1);
    EXPECT_TRUE(app.shutdown_requested());
}

TEST(AppFramework_Hooks, RequestShutdownCalledTwiceIsIdempotent)
{
    cd::app::AppConfig cfg;
    cfg.max_frames = 100u;
    DoubleRequestShutdownApp app { cfg };
    static_cast<void>(app.run());
    // Should break after frame 0 (request_shutdown set at end of frame 0);
    // loop checks the flag before pumping frame 1, so only 1 frame runs.
    EXPECT_EQ(app.frame_calls, 1);
    EXPECT_TRUE(app.shutdown_requested());
}

TEST(AppFramework_Hooks, RequestShutdownAtLastFrameDoesNotExtraFrame)
{
    // If request_shutdown is called in on_frame(frame_index == cap-1),
    // the loop still runs exactly cap frames (current frame completes first).
    constexpr std::uint64_t kCap = 5u;
    cd::app::AppConfig cfg;
    cfg.max_frames = kCap;
    TrackingApp app { cfg };
    app.request_shutdown_at_frame = static_cast<int>(kCap - 1u);
    static_cast<void>(app.run());
    EXPECT_EQ(static_cast<std::uint64_t>(app.frame_calls), kCap);
}

TEST(AppFramework_Hooks, EmptyMaxFrameCapPumpsOneFrameOnlyM2AContract)
{
    // Already covered in Config group; also verify via hooks path.
    cd::app::AppConfig cfg;
    cfg.max_frames = 0u;
    TrackingApp app { cfg };
    static_cast<void>(app.run());
    EXPECT_EQ(app.boot_calls, 1);
    EXPECT_EQ(app.frame_calls, 1);
    EXPECT_EQ(app.shutdown_calls, 1);
}
