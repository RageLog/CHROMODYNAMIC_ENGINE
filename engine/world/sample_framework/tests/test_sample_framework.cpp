// =============================================================================
// CHROMODYNAMIC -- engine/world/sample_framework/tests/test_sample_framework.cpp
// Mega-Marathon M2A skeleton (Run 29 / phase373).
//
// Exercises the App lifecycle contract WITHOUT a real RHI / window:
//   1. on_boot is called exactly once before any on_frame
//   2. on_frame is called max_frames times when no early shutdown
//   3. on_shutdown is called exactly once after the last on_frame
//   4. request_shutdown breaks the loop early
//   5. on_boot returning an error skips on_frame entirely
// =============================================================================
#include <cd/sample/App.hpp>
#include <cd/sample/run.hpp>

#include <cd/core/Result.hpp>
#include <utility>

#include <gtest/gtest.h>

namespace
{

class TrackingApp final : public cd::sample::App
{
public:
    using cd::sample::App::App;

    int boot_calls { 0 };
    int frame_calls { 0 };
    int shutdown_calls { 0 };
    bool boot_should_fail { false };
    int  request_shutdown_at_frame { -1 };
    std::uint64_t last_frame_index { 0u };
    double last_total_time { 0.0 };

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

    void on_frame(const cd::sample::FrameContext& fc) override
    {
        ++frame_calls;
        last_frame_index = fc.frame_index;
        last_total_time  = fc.total_time;
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

}  // namespace

TEST(SampleFramework_M2A, BootFrameShutdownSingleFramePath)
{
    cd::sample::AppConfig cfg;
    cfg.max_frames = 1u;
    TrackingApp app { cfg };
    const int rc = app.run();
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(app.boot_calls, 1);
    EXPECT_EQ(app.frame_calls, 1);
    EXPECT_EQ(app.shutdown_calls, 1);
    EXPECT_EQ(app.last_frame_index, 0u);
}

TEST(SampleFramework_M2A, MultiFramePumpAdvancesFrameIndex)
{
    cd::sample::AppConfig cfg;
    cfg.max_frames = 8u;
    TrackingApp app { cfg };
    EXPECT_EQ(app.run(), 0);
    EXPECT_EQ(app.frame_calls, 8);
    EXPECT_EQ(app.last_frame_index, 7u);
    EXPECT_NEAR(app.last_total_time, 7.0 / 60.0, 1e-6);
}

TEST(SampleFramework_M2A, RequestShutdownBreaksLoopEarly)
{
    cd::sample::AppConfig cfg;
    cfg.max_frames = 100u;
    TrackingApp app { cfg };
    app.request_shutdown_at_frame = 3;
    EXPECT_EQ(app.run(), 0);
    EXPECT_EQ(app.frame_calls, 4);  // frames 0..3 then break
    EXPECT_TRUE(app.shutdown_requested());
    EXPECT_EQ(app.shutdown_calls, 1);
}

TEST(SampleFramework_M2A, BootFailureSkipsFramePumpAndStillShutsDown)
{
    cd::sample::AppConfig cfg;
    cfg.max_frames = 5u;
    TrackingApp app { cfg };
    app.boot_should_fail = true;
    const int rc = app.run();
    EXPECT_NE(rc, 0);
    EXPECT_EQ(app.boot_calls, 1);
    EXPECT_EQ(app.frame_calls, 0);
    EXPECT_EQ(app.shutdown_calls, 1);
}

TEST(SampleFramework_M2A, RunTemplateEntryPointDrivesApp)
{
    cd::sample::AppConfig cfg;
    cfg.max_frames = 2u;
    // The template entry point is the canonical sample main() shape.
    const int rc = cd::sample::run<TrackingApp>(0, nullptr, cfg);
    EXPECT_EQ(rc, 0);
}
