// =============================================================================
// CHROMODYNAMIC — cd::diag tests (Sprint S2.1.e + S2.4)
// =============================================================================
#include <cd/diag/CrashReporter.hpp>
#include <cd/diag/DeadlineMonitor.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace
{

std::atomic<int> g_calls { 0 };
std::atomic<int> g_last_signal { 0 };
std::atomic<cd::diag::CrashSeverity> g_last_severity { cd::diag::CrashSeverity::Fatal };
char g_last_label[16] = {};

extern "C" void test_reporter(const cd::diag::CrashContext& ctx) noexcept
{
    g_calls.fetch_add(1, std::memory_order_relaxed);
    g_last_signal.store(ctx.raw_signal, std::memory_order_relaxed);
    g_last_severity.store(ctx.severity, std::memory_order_relaxed);
    // Copy small label into static buffer (async-signal-safe).
    const auto n = ctx.label.size() < sizeof(g_last_label) - 1 ? ctx.label.size() : sizeof(g_last_label) - 1;
    for (std::size_t i = 0; i < n; ++i)
    {
        g_last_label[i] = ctx.label[i];
    }
    g_last_label[n] = '\0';
}

class CrashReporterTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        g_calls.store(0);
        g_last_signal.store(0);
        g_last_severity.store(cd::diag::CrashSeverity::Fatal);
        g_last_label[0] = '\0';
    }
};

TEST_F(CrashReporterTest, InstallUninstall)
{
    cd::diag::CrashReporter cr;
    EXPECT_TRUE(cr.install(test_reporter));
    EXPECT_TRUE(cr.is_installed());
    cr.uninstall();
    EXPECT_FALSE(cr.is_installed());
}

TEST_F(CrashReporterTest, NullReporterRejected)
{
    cd::diag::CrashReporter cr;
    EXPECT_FALSE(cr.install(nullptr));
}

TEST_F(CrashReporterTest, DoubleInstallRejected)
{
    cd::diag::CrashReporter a, b;
    ASSERT_TRUE(a.install(test_reporter));
    EXPECT_FALSE(b.install(test_reporter));
}

TEST_F(CrashReporterTest, RaiseInterruptDispatches)
{
    cd::diag::CrashReporter cr;
    ASSERT_TRUE(cr.install(test_reporter));
    std::raise(SIGTERM);
    EXPECT_GE(g_calls.load(), 1);
    EXPECT_EQ(g_last_signal.load(), SIGTERM);
    EXPECT_EQ(g_last_severity.load(), cd::diag::CrashSeverity::Interrupt);
    EXPECT_STREQ(g_last_label, "SIGTERM");
    cr.uninstall();
}

TEST_F(CrashReporterTest, NonFatalDoesNotTerminate)
{
    cd::diag::CrashReporter cr;
    ASSERT_TRUE(cr.install(test_reporter));
    cr.capture_non_fatal("renderer-stall");
    EXPECT_EQ(g_calls.load(), 1);
    EXPECT_EQ(g_last_severity.load(), cd::diag::CrashSeverity::NonFatal);
    EXPECT_STREQ(g_last_label, "renderer-stall");
    cr.uninstall();
}

TEST_F(CrashReporterTest, DumpDirectoryRoundTrip)
{
    cd::diag::CrashReporter cr;
    cr.set_dump_directory("/tmp/cd_crash");
    EXPECT_EQ(cr.dump_directory(), "/tmp/cd_crash");
}

// --- DeadlineMonitor (Sprint S2.4 P2 salvage) -------------------------------
TEST(DeadlineMonitor, RegisterAndCount)
{
    cd::diag::DeadlineMonitor m;
    EXPECT_EQ(m.subsystem_count(), 0u);
    m.register_subsystem("render", std::chrono::milliseconds { 100 });
    m.register_subsystem("audio", std::chrono::milliseconds { 50 });
    EXPECT_EQ(m.subsystem_count(), 2u);
    m.unregister("render");
    EXPECT_EQ(m.subsystem_count(), 1u);
}

TEST(DeadlineMonitor, HeartbeatPreventsStall)
{
    cd::diag::DeadlineMonitor m;
    m.register_subsystem("render", std::chrono::milliseconds { 100 });
    m.heartbeat("render");
    EXPECT_EQ(m.evaluate(), 0u);
}

TEST(DeadlineMonitor, StallTriggersCallback)
{
    cd::diag::DeadlineMonitor m;
    std::atomic<int> stalled { 0 };
    std::string captured_name;
    std::mutex name_mutex;
    m.on_stall(
        [&](std::string_view name, std::chrono::milliseconds /*since*/)
        {
            stalled.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard guard { name_mutex };
            captured_name.assign(name);
        }
    );
    m.register_subsystem("idle", std::chrono::milliseconds { 5 });
    std::this_thread::sleep_for(std::chrono::milliseconds { 30 });
    EXPECT_EQ(m.evaluate(), 1u);
    EXPECT_EQ(stalled.load(), 1);
    std::lock_guard guard { name_mutex };
    EXPECT_EQ(captured_name, "idle");
}

TEST(DeadlineMonitor, HeartbeatUnknownIsNoop)
{
    cd::diag::DeadlineMonitor m;
    m.heartbeat("never-registered");  // must not crash, no entry created
    EXPECT_EQ(m.subsystem_count(), 0u);
}

}  // namespace
