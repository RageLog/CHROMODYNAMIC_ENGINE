// =============================================================================
// CHROMODYNAMIC -- cd::platform multi-window Sprint-1 tests.
//
// Phase 764 / FINALE-6 W1 -- verifies the cd::platform::Window primitive
// supports multiple simultaneous instances in one process, plus the new
// parent/child popout relationship and the aggregate pump_all_windows
// drain. Windows-only; non-Windows hosts compile this TU empty so the
// generated test binary still links and exits with success (an explicit
// "skipped" status for the Sprint-1 contract).
// =============================================================================
#include <cd/platform/Window.hpp>

#include <gtest/gtest.h>

#if defined(_WIN32)

    #include <array>
    #include <cstdint>
    #include <vector>

namespace
{

cd::platform::WindowDesc make_hidden_desc(std::string_view title) noexcept
{
    cd::platform::WindowDesc d {};
    d.title = title;
    d.width = 320;
    d.height = 240;
    d.visible = false;
    return d;
}

// ---------------------------------------------------------------------------
// MOMENT: A power user detaches the Inspector to a second monitor; both the
// main window and the popped-out Inspector live as independent OS windows.
// ---------------------------------------------------------------------------

TEST(PlatformMultiWindow, TwoIndependentWindowsCoexist)
{
    auto a = cd::platform::create_window(make_hidden_desc("cd_test_multi_a"));
    ASSERT_TRUE(a.has_value()) << a.error().message;

    auto b = cd::platform::create_window(make_hidden_desc("cd_test_multi_b"));
    ASSERT_TRUE(b.has_value()) << b.error().message;

    // Both real OS windows, each with its own HWND.
    ASSERT_NE((*a)->native_window_handle(), nullptr);
    ASSERT_NE((*b)->native_window_handle(), nullptr);
    EXPECT_NE((*a)->native_window_handle(), (*b)->native_window_handle());

    // Neither flagged for close yet.
    EXPECT_FALSE((*a)->should_close());
    EXPECT_FALSE((*b)->should_close());
}

TEST(PlatformMultiWindow, ClosingOneDoesNotAffectTheOther)
{
    auto a = cd::platform::create_window(make_hidden_desc("cd_test_multi_close_a"));
    ASSERT_TRUE(a.has_value());
    auto b = cd::platform::create_window(make_hidden_desc("cd_test_multi_close_b"));
    ASSERT_TRUE(b.has_value());

    (*a)->request_close();
    EXPECT_TRUE((*a)->should_close());
    EXPECT_FALSE((*b)->should_close());

    std::vector<cd::platform::OSEvent> events;
    EXPECT_FALSE((*a)->pump_events(events));  // closed window pump returns false
    EXPECT_TRUE((*b)->pump_events(events));   // peer remains alive
}

TEST(PlatformMultiWindow, PumpAllDrainsEveryLiveWindow)
{
    auto a = cd::platform::create_window(make_hidden_desc("cd_test_multi_pump_a"));
    ASSERT_TRUE(a.has_value());
    auto b = cd::platform::create_window(make_hidden_desc("cd_test_multi_pump_b"));
    ASSERT_TRUE(b.has_value());
    auto c = cd::platform::create_window(make_hidden_desc("cd_test_multi_pump_c"));
    ASSERT_TRUE(c.has_value());

    std::array<cd::platform::IWindow*, 3> wins { (*a).get(), (*b).get(), (*c).get() };

    std::vector<cd::platform::OSEvent> events;
    const bool any_alive = cd::platform::pump_all_windows(
        std::span<cd::platform::IWindow* const> { wins.data(), wins.size() },
        events
    );
    EXPECT_TRUE(any_alive);
    // No spurious kClose -- nobody asked to close.
    for (const auto& e : events)
        EXPECT_NE(e.kind, cd::platform::OSEventKind::kClose);

    // Close all three; pump_all_windows now returns false.
    (*a)->request_close();
    (*b)->request_close();
    (*c)->request_close();
    events.clear();
    const bool still_alive = cd::platform::pump_all_windows(
        std::span<cd::platform::IWindow* const> { wins.data(), wins.size() },
        events
    );
    EXPECT_FALSE(still_alive);
}

TEST(PlatformMultiWindow, SetParentEstablishesPopupRelationship)
{
    auto parent = cd::platform::create_window(make_hidden_desc("cd_test_multi_parent"));
    ASSERT_TRUE(parent.has_value());
    auto child = cd::platform::create_window(make_hidden_desc("cd_test_multi_child"));
    ASSERT_TRUE(child.has_value());

    // Attach child to parent -- Win32 backend rewires WS_POPUP + SetParent.
    EXPECT_TRUE((*child)->set_parent((*parent).get()));

    // Both handles still independent and addressable.
    EXPECT_NE((*child)->native_window_handle(), (*parent)->native_window_handle());

    // Detach restores overlapped style.
    EXPECT_TRUE((*child)->set_parent(nullptr));
}

TEST(PlatformMultiWindow, PumpAllSkipsNullEntries)
{
    auto a = cd::platform::create_window(make_hidden_desc("cd_test_multi_null"));
    ASSERT_TRUE(a.has_value());

    std::array<cd::platform::IWindow*, 3> wins { nullptr, (*a).get(), nullptr };
    std::vector<cd::platform::OSEvent> events;
    EXPECT_TRUE(cd::platform::pump_all_windows(
        std::span<cd::platform::IWindow* const> { wins.data(), wins.size() },
        events
    ));
}

}  // namespace

#else  // !_WIN32

TEST(PlatformMultiWindow, SkippedOnNonWindowsSprint1)
{
    // Sprint-1 contract: Windows-only. Linux X11 / macOS Cocoa parenting and
    // multi-window pump are tracked for Sprint-2.
    GTEST_SKIP() << "cd::platform multi-window Sprint-1 is Windows-only";
}

#endif  // _WIN32
