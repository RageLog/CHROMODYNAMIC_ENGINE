// =============================================================================
// CHROMODYNAMIC -- cd::ui::widgets::NativeWindowAdapter tests
//
// Phase 706 — Sprint-2 native-window adapter coverage.
//
// Test strategy
// -------------
// NativeWindowAdapter calls cd::platform::create_window() which on Windows
// creates a real HWND. On other platforms (or headless CI without a display
// server) it returns kNotImplemented and the adapter falls back to stub mode.
//
// All 6 tests are written to pass on BOTH paths:
//   - Real OS window path  (Win32, visible display)
//   - Stub / gap path      (unsupported platform or kNotImplemented)
//
// Tests do NOT depend on a swapchain, GPU, or ImGui — pure state machine.
//
// 6 test cases:
//   1. open() adds panel to managed_windows().
//   2. open() same panel twice is idempotent — second call returns false,
//      managed count stays 1.
//   3. close() removes the panel from managed_windows().
//   4. close() of unknown panel is a no-op (no crash).
//   5. is_managed() reflects open/close round-trip.
//   6. On stub path, gaps() records the gap with non-empty unblock_api.
//      On real path, gaps() is empty.
// =============================================================================
#include <cd/ui/widgets/NativeWindowAdapter.hpp>
#include <cd/ui/widgets/PopoutDock.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace w = cd::ui::widgets;

namespace
{

constexpr std::array<float, 2U> kPos  { 50.0F, 50.0F };
constexpr std::array<float, 2U> kSize { 320.0F, 240.0F };

/// Helper: build a PopoutWindow for a given panel_id.
w::PopoutWindow make_pw(std::string_view id)
{
    return w::PopoutWindow {
        .panel_id  = std::string { id },
        .position  = kPos,
        .size      = kSize,
        .is_active = true,
    };
}

}  // namespace

// =============================================================================
// Case 1: open() adds the panel to managed_windows().
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, OpenAddsToManagedWindows)
{
    w::NativeWindowAdapter adapter;
    const auto pw = make_pw("Inspector");

    // open() returns true on real window path, false on stub path — both valid.
    const bool result = adapter.open(pw);
    (void)result;  // platform-dependent; we only assert the managed state.

    EXPECT_TRUE(adapter.is_managed("Inspector"));
    ASSERT_EQ(adapter.managed_windows().size(), 1U);
    EXPECT_EQ(adapter.managed_windows()[0].panel_id, "Inspector");
}

// =============================================================================
// Case 2: open() the same panel twice is idempotent.
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, OpenSamePanelTwiceIsIdempotent)
{
    w::NativeWindowAdapter adapter;
    const auto pw = make_pw("Console");

    static_cast<void>(adapter.open(pw));
    const bool second = adapter.open(pw);  // must return false

    EXPECT_FALSE(second);
    EXPECT_EQ(adapter.managed_windows().size(), 1U);
}

// =============================================================================
// Case 3: close() removes the panel from managed_windows().
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, CloseRemovesPanel)
{
    w::NativeWindowAdapter adapter;
    const auto pw = make_pw("Outliner");

    static_cast<void>(adapter.open(pw));
    ASSERT_TRUE(adapter.is_managed("Outliner"));

    adapter.close("Outliner");

    EXPECT_FALSE(adapter.is_managed("Outliner"));
    EXPECT_TRUE(adapter.managed_windows().empty());
}

// =============================================================================
// Case 4: close() of an unknown panel is a no-op (no crash, no state change).
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, CloseUnknownPanelIsNoOp)
{
    w::NativeWindowAdapter adapter;
    // Precondition: one real managed window.
    static_cast<void>(adapter.open(make_pw("Inspector")));
    ASSERT_EQ(adapter.managed_windows().size(), 1U);

    // Close something we never opened.
    adapter.close("NonExistentPanel");

    // Existing window must be unaffected.
    EXPECT_EQ(adapter.managed_windows().size(), 1U);
    EXPECT_TRUE(adapter.is_managed("Inspector"));
}

// =============================================================================
// Case 5: is_managed() reflects the open/close round-trip.
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, IsManagedReflectsRoundTrip)
{
    w::NativeWindowAdapter adapter;

    EXPECT_FALSE(adapter.is_managed("Inspector"));

    static_cast<void>(adapter.open(make_pw("Inspector")));
    EXPECT_TRUE(adapter.is_managed("Inspector"));

    adapter.close("Inspector");
    EXPECT_FALSE(adapter.is_managed("Inspector"));
}

// =============================================================================
// Case 6: Stub / gap path — gaps() records useful diagnostic when platform
//         returns kNotImplemented. Real path — gaps() is empty.
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, StubPathRecordsGapOrRealPathHasNoGaps)
{
    w::NativeWindowAdapter adapter;
    const auto pw = make_pw("AssetBrowser");

    const bool real_window_created = adapter.open(pw);

    if (real_window_created)
    {
        // Real OS window was created — no gaps expected.
        EXPECT_TRUE(adapter.gaps().empty())
            << "A real OS window was created; gaps() must be empty.";
        EXPECT_EQ(adapter.managed_windows()[0].is_stub, false);
    }
    else
    {
        // Stub path — a gap entry must exist with useful strings.
        ASSERT_EQ(adapter.gaps().size(), 1U)
            << "Stub path must record exactly one gap entry.";

        const auto& gap = adapter.gaps()[0];
        EXPECT_EQ(gap.panel_id, "AssetBrowser");
        EXPECT_FALSE(gap.reason.empty())
            << "Gap reason must not be empty.";
        EXPECT_FALSE(gap.unblock_api.empty())
            << "Gap unblock_api must document which API unblocks real multi-window.";

        // Stub entry must still be tracked so Sprint-1 floating-internal
        // rendering can take over.
        ASSERT_EQ(adapter.managed_windows().size(), 1U);
        EXPECT_TRUE(adapter.managed_windows()[0].is_stub);
    }
}
