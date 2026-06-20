// =============================================================================
// CHROMODYNAMIC — cd::diag tests (Sprint S2.1.e + S2.4)
// =============================================================================
#include <cd/diag/Assert.hpp>
#include <cd/diag/CrashReporter.hpp>
#include <cd/diag/DeadlineMonitor.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

std::atomic<int> g_calls { 0 };
std::atomic<int> g_last_signal { 0 };
std::atomic<cd::diag::CrashSeverity> g_last_severity { cd::diag::CrashSeverity::kFatal };
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
        g_last_severity.store(cd::diag::CrashSeverity::kFatal);
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
    cd::diag::CrashReporter a;
    cd::diag::CrashReporter b;
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
    EXPECT_EQ(g_last_severity.load(), cd::diag::CrashSeverity::kInterrupt);
    EXPECT_STREQ(g_last_label, "SIGTERM");
    cr.uninstall();
}

TEST_F(CrashReporterTest, NonFatalDoesNotTerminate)
{
    cd::diag::CrashReporter cr;
    ASSERT_TRUE(cr.install(test_reporter));
    cr.capture_non_fatal("renderer-stall");
    EXPECT_EQ(g_calls.load(), 1);
    EXPECT_EQ(g_last_severity.load(), cd::diag::CrashSeverity::kNonFatal);
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
    // Injected-clock version — no sleep_for, deterministic.
    using TP = cd::diag::DeadlineMonitor::TimePoint;
    std::atomic<TP> fake_time { TP {} };
    cd::diag::DeadlineMonitor m { [&]() { return fake_time.load(std::memory_order_acquire); } };

    std::atomic<int> stalled { 0 };
    std::string captured_name;
    std::mutex name_mutex;
    m.on_stall(
        [&](std::string_view name, std::chrono::milliseconds /*since*/)
        {
            stalled.fetch_add(1, std::memory_order_relaxed);
            std::scoped_lock guard { name_mutex };
            captured_name.assign(name);
        }
    );

    const auto t0 = TP {};
    fake_time.store(t0, std::memory_order_release);
    m.register_subsystem("idle", std::chrono::milliseconds { 100 });

    // Advance clock past deadline — evaluate should fire callback.
    fake_time.store(t0 + std::chrono::milliseconds { 200 }, std::memory_order_release);
    EXPECT_EQ(m.evaluate(), 1u);
    EXPECT_EQ(stalled.load(), 1);
    std::scoped_lock guard { name_mutex };
    EXPECT_EQ(captured_name, "idle");
}

TEST(DeadlineMonitor, HeartbeatUnknownIsNoop)
{
    cd::diag::DeadlineMonitor m;
    m.heartbeat("never-registered");  // must not crash, no entry created
    EXPECT_EQ(m.subsystem_count(), 0u);
}

// --- Assert macros (Wave 107) ---------------------------------------------
//
// The macros must terminate the process if the handler returns; for
// unit tests we install a throwing handler so we can observe failure
// without aborting. The handler stays installed for the duration of
// each test via an RAII guard.

namespace
{

struct ThrowingPanicGuard
{
    cd::diag::PanicHandler prev;
    ThrowingPanicGuard() : prev { cd::diag::set_panic_handler(&throw_handler) } {}
    ~ThrowingPanicGuard() { (void)cd::diag::set_panic_handler(prev); }

    static thread_local cd::diag::PanicInfo last_info;
    static void throw_handler(const cd::diag::PanicInfo& info)
    {
        last_info = info;
        // String copies into the thread_local so the std::string_view
        // members stay valid after the macro's local std::source_location
        // goes out of scope.
        captured_file = std::string { info.file };
        captured_expr = std::string { info.expression };
        captured_msg = std::string { info.message };
        last_info.file = captured_file;
        last_info.expression = captured_expr;
        last_info.message = captured_msg;
        throw std::runtime_error { "panic captured" };
    }
    static thread_local std::string captured_file;
    static thread_local std::string captured_expr;
    static thread_local std::string captured_msg;
};

thread_local cd::diag::PanicInfo ThrowingPanicGuard::last_info {};
thread_local std::string ThrowingPanicGuard::captured_file {};
thread_local std::string ThrowingPanicGuard::captured_expr {};
thread_local std::string ThrowingPanicGuard::captured_msg {};

}  // namespace

TEST(Assert, VerifyPassesOnTrue)
{
    ThrowingPanicGuard guard;
    EXPECT_NO_THROW(CD_VERIFY(2 + 2 == 4));
}

TEST(Assert, VerifyThrowsOnFalse)
{
    ThrowingPanicGuard guard;
    EXPECT_THROW(CD_VERIFY(1 == 2), std::runtime_error);
    EXPECT_NE(ThrowingPanicGuard::captured_expr.find("1 == 2"), std::string::npos);
    EXPECT_GT(ThrowingPanicGuard::last_info.line, 0U);
}

TEST(Assert, VerifyMsgCarriesMessage)
{
    ThrowingPanicGuard guard;
    EXPECT_THROW(CD_VERIFY_MSG(false, "custom failure"), std::runtime_error);
    EXPECT_EQ(ThrowingPanicGuard::captured_msg, "custom failure");
}

TEST(Assert, PanicUnconditionalFires)
{
    ThrowingPanicGuard guard;
    EXPECT_THROW(CD_PANIC("intentional"), std::runtime_error);
    EXPECT_EQ(ThrowingPanicGuard::captured_msg, "intentional");
}

TEST(Assert, SetHandlerReturnsPrevious)
{
    auto a = cd::diag::current_panic_handler();
    auto b = cd::diag::set_panic_handler(&ThrowingPanicGuard::throw_handler);
    EXPECT_EQ(b, a);
    auto c = cd::diag::reset_panic_handler();
    EXPECT_EQ(c, &ThrowingPanicGuard::throw_handler);
}

#if !defined(NDEBUG)
TEST(Assert, DebugAssertFiresInDebugBuilds)
{
    ThrowingPanicGuard guard;
    EXPECT_THROW(CD_ASSERT(false), std::runtime_error);
}
#endif

// =============================================================================
// NEW: Gap-closing tests — phase 1258 (cd::diag 82→100)
// =============================================================================

// --- Assert: additional coverage --------------------------------------------

/// set_panic_handler(nullptr) must silently install the default handler,
/// not leave a null function-pointer in the slot.
TEST(Assert, SetNullHandlerResetsToDefault)
{
    // Arrange: stash current handler.
    const auto original = cd::diag::current_panic_handler();

    // Act: set nullptr — must not store null.
    (void)cd::diag::set_panic_handler(nullptr);

    // Assert: current_panic_handler() is never null.
    EXPECT_NE(cd::diag::current_panic_handler(), nullptr);

    // Restore.
    (void)cd::diag::set_panic_handler(original);
}

/// PanicInfo.file must be non-empty (contains at least the filename).
TEST(Assert, PanicInfoFileIsNonEmpty)
{
    ThrowingPanicGuard guard;
    try
    {
        CD_VERIFY(false);
    }
    catch (const std::runtime_error&)
    {
        SUCCEED();  // expected throw; the captured panic state is asserted below
    }
    EXPECT_FALSE(ThrowingPanicGuard::captured_file.empty());
}

/// PanicInfo.function must be non-empty when thrown from a named function.
TEST(Assert, PanicInfoFunctionIsNonEmpty)
{
    ThrowingPanicGuard guard;
    try
    {
        CD_VERIFY(false);
    }
    catch (const std::runtime_error&)
    {
        SUCCEED();  // expected throw; the captured panic state is asserted below
    }
    EXPECT_FALSE(ThrowingPanicGuard::last_info.function.empty());
}

/// PanicInfo.line must be greater than zero at the macro call site.
TEST(Assert, PanicInfoLineIsPositive)
{
    ThrowingPanicGuard guard;
    try
    {
        CD_PANIC("line-check");
    }
    catch (const std::runtime_error&)
    {
        SUCCEED();  // expected throw; the captured panic state is asserted below
    }
    EXPECT_GT(ThrowingPanicGuard::last_info.line, 0U);
}

/// CD_ASSERT_MSG propagates the custom message even in debug builds.
#if !defined(NDEBUG)
TEST(Assert, AssertMsgCarriesMessage)
{
    ThrowingPanicGuard guard;
    EXPECT_THROW(CD_ASSERT_MSG(false, "my custom msg"), std::runtime_error);
    EXPECT_EQ(ThrowingPanicGuard::captured_msg, "my custom msg");
}
#endif

/// CD_VERIFY_MSG — expression string is correctly captured.
TEST(Assert, VerifyMsgExpressionCaptured)
{
    ThrowingPanicGuard guard;
    try
    {
        CD_VERIFY_MSG(1 == 2, "expr-test"); // NOLINT(misc-redundant-expression)
    }
    catch (const std::runtime_error&)
    {
        SUCCEED();  // expected throw; the captured panic state is asserted below
    }
    EXPECT_NE(ThrowingPanicGuard::captured_expr.find("1 == 2"), std::string::npos);
}

/// CD_PANIC — expression field is empty (no expression for unconditional panic).
TEST(Assert, PanicExpressionIsEmpty)
{
    ThrowingPanicGuard guard;
    try
    {
        CD_PANIC("expr-empty-check");
    }
    catch (const std::runtime_error&)
    {
        SUCCEED();  // expected throw; the captured panic state is asserted below
    }
    EXPECT_TRUE(ThrowingPanicGuard::captured_expr.empty());
}

/// reset_panic_handler restores default and the default is callable
/// (i.e., it must not be null after reset).
TEST(Assert, ResetPanicHandlerIsNonNull)
{
    (void)cd::diag::set_panic_handler(&ThrowingPanicGuard::throw_handler);
    (void)cd::diag::reset_panic_handler();
    EXPECT_NE(cd::diag::current_panic_handler(), nullptr);
}

// --- CrashReporter: additional coverage -------------------------------------

/// CrashSeverity::kFatal is distinguishable from kNonFatal and kInterrupt.
TEST_F(CrashReporterTest, SeverityEnumDistinct)
{
    EXPECT_NE(static_cast<int>(cd::diag::CrashSeverity::kFatal),
              static_cast<int>(cd::diag::CrashSeverity::kNonFatal));
    EXPECT_NE(static_cast<int>(cd::diag::CrashSeverity::kFatal),
              static_cast<int>(cd::diag::CrashSeverity::kInterrupt));
    EXPECT_NE(static_cast<int>(cd::diag::CrashSeverity::kNonFatal),
              static_cast<int>(cd::diag::CrashSeverity::kInterrupt));
}

/// capture_non_fatal with no reporter installed must not crash.
TEST_F(CrashReporterTest, NonFatalWithNoReporterIsSafe)
{
    cd::diag::CrashReporter cr;
    // No install() — must be a no-op.
    EXPECT_NO_FATAL_FAILURE(cr.capture_non_fatal("no-reporter-safe"));
    EXPECT_FALSE(cr.is_installed());
}

/// Double uninstall must be a silent no-op.
TEST_F(CrashReporterTest, DoubleUninstallIsNoop)
{
    cd::diag::CrashReporter cr;
    ASSERT_TRUE(cr.install(test_reporter));
    cr.uninstall();
    EXPECT_NO_FATAL_FAILURE(cr.uninstall());
    EXPECT_FALSE(cr.is_installed());
}

/// Destructor uninstalls automatically; is_installed() reflects it.
TEST_F(CrashReporterTest, DestructorUninstalls)
{
    {
        cd::diag::CrashReporter cr;
        ASSERT_TRUE(cr.install(test_reporter));
        EXPECT_TRUE(cr.is_installed());
    }
    // After destruction, another reporter can be installed (global flag cleared).
    cd::diag::CrashReporter cr2;
    EXPECT_TRUE(cr2.install(test_reporter));
    cr2.uninstall();
}

/// SIGINT routes to kInterrupt severity with correct label.
TEST_F(CrashReporterTest, RaiseSignalIntDispatches)
{
    cd::diag::CrashReporter cr;
    ASSERT_TRUE(cr.install(test_reporter));
    std::raise(SIGINT);
    EXPECT_GE(g_calls.load(), 1);
    EXPECT_EQ(g_last_signal.load(), SIGINT);
    EXPECT_EQ(g_last_severity.load(), cd::diag::CrashSeverity::kInterrupt);
    EXPECT_STREQ(g_last_label, "SIGINT");
    cr.uninstall();
}

/// capture_non_fatal passes an arbitrary label through unchanged.
TEST_F(CrashReporterTest, NonFatalLabelPassthrough)
{
    cd::diag::CrashReporter cr;
    ASSERT_TRUE(cr.install(test_reporter));
    cr.capture_non_fatal("audio-xrun");
    EXPECT_EQ(g_last_severity.load(), cd::diag::CrashSeverity::kNonFatal);
    EXPECT_STREQ(g_last_label, "audio-xrun");
    EXPECT_EQ(g_last_signal.load(), 0);
    cr.uninstall();
}

/// After uninstall, capture_non_fatal does nothing (reporter pointer cleared).
TEST_F(CrashReporterTest, NonFatalAfterUninstallIsNoop)
{
    cd::diag::CrashReporter cr;
    ASSERT_TRUE(cr.install(test_reporter));
    cr.uninstall();
    const auto calls_before = g_calls.load();
    cr.capture_non_fatal("post-uninstall");
    EXPECT_EQ(g_calls.load(), calls_before);
}

// --- DeadlineMonitor: injected-clock + arm/disarm/extend -------------------

/// Helper: build a DeadlineMonitor with a controllable fake clock.
namespace
{

struct FakeClock
{
    using TP = cd::diag::DeadlineMonitor::TimePoint;
    std::atomic<TP> current { TP {} };

    [[nodiscard]] TP now() const { return current.load(std::memory_order_acquire); }
    void advance(std::chrono::milliseconds delta)
    {
        current.store(current.load(std::memory_order_acquire) + delta, std::memory_order_release);
    }
};

}  // namespace

/// Heartbeat at t=0 keeps subsystem healthy at t=50ms (deadline=100ms).
TEST(DeadlineMonitor, InjectedClockNoStallBeforeDeadline)
{
    FakeClock clk;
    cd::diag::DeadlineMonitor m { [&]() { return clk.now(); } };

    m.register_subsystem("render", std::chrono::milliseconds { 100 });
    m.heartbeat("render");
    clk.advance(std::chrono::milliseconds { 50 });
    EXPECT_EQ(m.evaluate(), 0u);
}

/// No heartbeat: advancing past deadline triggers stall exactly once per evaluate().
TEST(DeadlineMonitor, InjectedClockStallAfterDeadline)
{
    FakeClock clk;
    cd::diag::DeadlineMonitor m { [&]() { return clk.now(); } };

    std::atomic<int> fires { 0 };
    m.on_stall([&](std::string_view /*name*/, std::chrono::milliseconds /*s*/) {
        fires.fetch_add(1, std::memory_order_relaxed);
    });

    m.register_subsystem("audio", std::chrono::milliseconds { 100 });
    clk.advance(std::chrono::milliseconds { 150 });
    EXPECT_EQ(m.evaluate(), 1u);
    EXPECT_EQ(fires.load(), 1);
}

/// Heartbeat refreshes the window — stall should NOT fire immediately after.
TEST(DeadlineMonitor, InjectedClockHeartbeatResetsWindow)
{
    FakeClock clk;
    cd::diag::DeadlineMonitor m { [&]() { return clk.now(); } };

    std::atomic<int> fires { 0 };
    m.on_stall([&](std::string_view, std::chrono::milliseconds) {
        fires.fetch_add(1, std::memory_order_relaxed);
    });

    m.register_subsystem("asset", std::chrono::milliseconds { 100 });
    clk.advance(std::chrono::milliseconds { 90 });
    m.heartbeat("asset");         // reset window at t=90
    clk.advance(std::chrono::milliseconds { 50 }); // now t=140, only 50ms since last hb
    EXPECT_EQ(m.evaluate(), 0u);
    EXPECT_EQ(fires.load(), 0);
}

/// extend_deadline makes a subsystem tolerate a longer gap.
TEST(DeadlineMonitor, ExtendDeadlinePreventsStall)
{
    FakeClock clk;
    cd::diag::DeadlineMonitor m { [&]() { return clk.now(); } };

    std::atomic<int> fires { 0 };
    m.on_stall([&](std::string_view, std::chrono::milliseconds) {
        fires.fetch_add(1, std::memory_order_relaxed);
    });

    m.register_subsystem("loader", std::chrono::milliseconds { 100 });
    m.extend_deadline("loader", std::chrono::milliseconds { 500 });
    clk.advance(std::chrono::milliseconds { 200 });
    // 200ms elapsed but deadline is now 500ms — no stall.
    EXPECT_EQ(m.evaluate(), 0u);
    EXPECT_EQ(fires.load(), 0);
}

/// extend_deadline on unknown subsystem is a no-op (no crash).
TEST(DeadlineMonitor, ExtendDeadlineUnknownIsNoop)
{
    cd::diag::DeadlineMonitor m;
    EXPECT_NO_FATAL_FAILURE(m.extend_deadline("ghost", std::chrono::milliseconds { 999 }));
    EXPECT_EQ(m.subsystem_count(), 0u);
}

/// disarm suppresses stall even after the deadline elapses.
TEST(DeadlineMonitor, DisarmSuppressesStall)
{
    FakeClock clk;
    cd::diag::DeadlineMonitor m { [&]() { return clk.now(); } };

    std::atomic<int> fires { 0 };
    m.on_stall([&](std::string_view, std::chrono::milliseconds) {
        fires.fetch_add(1, std::memory_order_relaxed);
    });

    m.register_subsystem("physics", std::chrono::milliseconds { 100 });
    m.disarm("physics");
    EXPECT_FALSE(m.is_armed("physics"));
    clk.advance(std::chrono::milliseconds { 500 });
    EXPECT_EQ(m.evaluate(), 0u);
    EXPECT_EQ(fires.load(), 0);
}

/// arm() re-enables a disarmed subsystem.
TEST(DeadlineMonitor, ArmReenablesStall)
{
    FakeClock clk;
    cd::diag::DeadlineMonitor m { [&]() { return clk.now(); } };

    std::atomic<int> fires { 0 };
    m.on_stall([&](std::string_view, std::chrono::milliseconds) {
        fires.fetch_add(1, std::memory_order_relaxed);
    });

    m.register_subsystem("network", std::chrono::milliseconds { 100 });
    m.disarm("network");
    clk.advance(std::chrono::milliseconds { 50 });

    // Re-arm: last_heartbeat refreshes to current clock (t=50).
    m.arm("network");
    EXPECT_TRUE(m.is_armed("network"));

    // Advance only 80ms more (total t=130, but hb reset at t=50 → 80ms since hb).
    clk.advance(std::chrono::milliseconds { 80 });
    EXPECT_EQ(m.evaluate(), 0u); // 80ms < 100ms deadline

    // Advance another 50ms (130ms since hb) → stall.
    clk.advance(std::chrono::milliseconds { 50 });
    EXPECT_EQ(m.evaluate(), 1u);
    EXPECT_EQ(fires.load(), 1);
}

/// Multiple subsystems: only the expired one fires.
TEST(DeadlineMonitor, MultipleSubsystemsOnlyExpiredFires)
{
    FakeClock clk;
    cd::diag::DeadlineMonitor m { [&]() { return clk.now(); } };

    std::vector<std::string> fired_names;
    std::mutex fired_mutex;
    m.on_stall([&](std::string_view name, std::chrono::milliseconds) {
        std::scoped_lock g { fired_mutex };
        fired_names.emplace_back(name);
    });

    m.register_subsystem("fast", std::chrono::milliseconds { 1000 });
    m.register_subsystem("slow", std::chrono::milliseconds { 50 });

    clk.advance(std::chrono::milliseconds { 100 });
    EXPECT_EQ(m.evaluate(), 1u);

    std::scoped_lock g { fired_mutex };
    ASSERT_EQ(fired_names.size(), 1u);
    EXPECT_EQ(fired_names[0], "slow");
}

/// is_armed returns false for unknown subsystems.
TEST(DeadlineMonitor, IsArmedReturnsFalseForUnknown)
{
    cd::diag::DeadlineMonitor m;
    EXPECT_FALSE(m.is_armed("not-registered"));
}

}  // namespace
