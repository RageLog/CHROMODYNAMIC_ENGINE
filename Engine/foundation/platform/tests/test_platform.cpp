// =============================================================================
// CHROMODYNAMIC — cd::platform tests (Sprint S2.1.e)
//
// Note: signal handler tests intentionally avoid raising real crash signals
// (which would terminate the test process). We test install/uninstall lifecyle
// and interrupt-class signal dispatch only.
// =============================================================================
#include <cd/platform/SignalHandler.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <vector>

namespace
{

std::atomic<int> g_invocations { 0 };
std::atomic<int> g_last_signal { 0 };
std::atomic<cd::platform::SignalCategory> g_last_category { cd::platform::SignalCategory::Unknown };

extern "C" void test_callback(cd::platform::SignalCategory category, int raw) noexcept
{
    g_invocations.fetch_add(1, std::memory_order_relaxed);
    g_last_signal.store(raw, std::memory_order_relaxed);
    g_last_category.store(category, std::memory_order_relaxed);
}

TEST(SignalHandler, NotActiveByDefault)
{
    EXPECT_FALSE(cd::platform::SignalHandler::is_active());
}

TEST(SignalHandler, InstallUninstallRoundTrip)
{
    cd::platform::SignalHandler h;
    EXPECT_TRUE(h.install(test_callback));
    EXPECT_TRUE(h.is_installed());
    EXPECT_TRUE(cd::platform::SignalHandler::is_active());
    h.uninstall();
    EXPECT_FALSE(h.is_installed());
    EXPECT_FALSE(cd::platform::SignalHandler::is_active());
}

TEST(SignalHandler, DoubleInstallReturnsFalse)
{
    cd::platform::SignalHandler a;
    cd::platform::SignalHandler b;
    ASSERT_TRUE(a.install(test_callback));
    EXPECT_FALSE(b.install(test_callback));
    a.uninstall();
}

TEST(SignalHandler, NullCallbackRejected)
{
    cd::platform::SignalHandler h;
    EXPECT_FALSE(h.install(nullptr));
    EXPECT_FALSE(h.is_installed());
}

TEST(SignalHandler, DispatchOnRaisedInterrupt)
{
    g_invocations.store(0);
    g_last_signal.store(0);
    g_last_category.store(cd::platform::SignalCategory::Unknown);

    cd::platform::SignalHandler h;
    ASSERT_TRUE(h.install(test_callback));

    // Raise an interrupt signal manually; the process default behaviour for
    // SIGTERM would be termination, but we've replaced it with our dispatch.
    std::raise(SIGTERM);

    EXPECT_GE(g_invocations.load(), 1);
    EXPECT_EQ(g_last_signal.load(), SIGTERM);
    EXPECT_EQ(g_last_category.load(), cd::platform::SignalCategory::Interrupt);

    h.uninstall();
}

TEST(SignalHandler, ScopedUninstallOnDestruction)
{
    {
        cd::platform::SignalHandler scoped;
        ASSERT_TRUE(scoped.install(test_callback));
        EXPECT_TRUE(cd::platform::SignalHandler::is_active());
    }  // ~SignalHandler() runs uninstall()
    EXPECT_FALSE(cd::platform::SignalHandler::is_active());
}

}  // namespace

// ============================================================================
// S6.1 — IWindow / Win32Window smoke tests. Windows-only path; non-Windows
// hosts skip via the kNotImplemented branch.
// ============================================================================
#include <cd/platform/Window.hpp>

namespace
{

TEST(Window, RejectsZeroExtent)
{
    cd::platform::WindowDesc d {};
    d.width = 0;
    d.height = 720;
    auto w = cd::platform::create_window(d);
    ASSERT_FALSE(w.has_value());
    EXPECT_EQ(w.error().code, static_cast<std::uint32_t>(cd::platform::platform_errors::Code::kInvalidArgument));
}

#if defined(_WIN32)

TEST(Window, CreateHiddenWindowSucceeds)
{
    cd::platform::WindowDesc d {};
    d.title = "cd_test_platform_window";
    d.width = 320;
    d.height = 240;
    d.visible = false;
    auto w = cd::platform::create_window(d);
    ASSERT_TRUE(w.has_value()) << w.error().message;
    EXPECT_NE((*w)->native_window_handle(), nullptr);
    EXPECT_NE((*w)->native_display_handle(), nullptr);
    EXPECT_EQ((*w)->width(), 320U);
    EXPECT_EQ((*w)->height(), 240U);
    EXPECT_FALSE((*w)->should_close());
}

TEST(Window, RequestCloseFlipsShouldCloseAndStopsPump)
{
    cd::platform::WindowDesc d {};
    d.title = "cd_test_close";
    d.visible = false;
    auto w = cd::platform::create_window(d);
    ASSERT_TRUE(w.has_value());
    (*w)->request_close();
    EXPECT_TRUE((*w)->should_close());
    std::vector<cd::platform::OSEvent> events;
    EXPECT_FALSE((*w)->pump_events(events));
}

TEST(Window, PumpEventsDrainsQueueClean)
{
    // A freshly-created hidden window has no pending input but does receive
    // WM_SIZE / WM_SHOWWINDOW etc. from CreateWindowEx. Verify pump returns
    // true (no close) and any events we get carry well-formed payloads.
    cd::platform::WindowDesc d {};
    d.title = "cd_test_pump";
    d.visible = false;
    auto w = cd::platform::create_window(d);
    ASSERT_TRUE(w.has_value());
    std::vector<cd::platform::OSEvent> events;
    EXPECT_TRUE((*w)->pump_events(events));
    // Either zero or a handful of system-generated events; none of them
    // should be a kClose unless the user closed the window.
    for (const auto& e : events)
    {
        EXPECT_NE(e.kind, cd::platform::OSEventKind::kClose);
    }
}

TEST(Window, SetTitleDoesNotCrash)
{
    cd::platform::WindowDesc d {};
    d.visible = false;
    auto w = cd::platform::create_window(d);
    ASSERT_TRUE(w.has_value());
    (*w)->set_title("renamed");
}

#endif  // _WIN32

}  // namespace
