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
//
// Band-2-UI top-up (2026-06-16): the GAP-2 aggregate per-frame surface
// (pump_all + collect_closed_panels) had ZERO coverage. Cases 7-9 close
// that gap. They are path-agnostic: on the stub path both methods iterate
// stub entries (no-op) and on the real Win32 path they drain the OS event
// queue / harvest native-close requests — neither may crash and the
// freshly-created window must not report should_close().
// =============================================================================
#include <cd/ui/widgets/NativeWindowAdapter.hpp>
#include <cd/ui/widgets/PopoutDock.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

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

// =============================================================================
// Case 7: pump_all() drains events without crashing on BOTH paths and emits
//         only events tagged with a managed panel_id. Immediately after
//         open() the freshly created window has no pending OS events, so the
//         tagged-event vector is empty (or, defensively, only carries the
//         just-opened panel's id) — never an unmanaged id.
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, PumpAllIsSafeAndTagsByPanel)
{
    w::NativeWindowAdapter adapter;
    static_cast<void>(adapter.open(make_pw("Inspector")));
    ASSERT_EQ(adapter.managed_windows().size(), 1U);

    std::vector<w::NativeWindowAdapter::TaggedEvent> events;
    // Must not crash on stub (no-op over stub entry) nor on real Win32
    // (drains the empty pending queue of the just-created window).
    adapter.pump_all(events);

    // A brand-new window has no queued OS events; if any leaked through they
    // MUST be tagged with a currently-managed panel id (never an alien id).
    for (const auto& te : events)
    {
        EXPECT_TRUE(adapter.is_managed(te.panel_id))
            << "pump_all emitted an event tagged with an unmanaged panel id: "
            << te.panel_id;
    }

    // pump_all is non-mutating w.r.t. the managed set.
    EXPECT_EQ(adapter.managed_windows().size(), 1U);
    EXPECT_TRUE(adapter.is_managed("Inspector"));
}

// =============================================================================
// Case 8: pump_all() with NO managed windows is a clean no-op (empty out).
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, PumpAllEmptyAdapterIsNoOp)
{
    w::NativeWindowAdapter adapter;

    std::vector<w::NativeWindowAdapter::TaggedEvent> events;
    events.push_back(w::NativeWindowAdapter::TaggedEvent {});  // pre-seed
    adapter.pump_all(events);

    // pump_all appends; it must not have added anything (no managed windows).
    EXPECT_EQ(events.size(), 1U);
    EXPECT_TRUE(adapter.managed_windows().empty());
}

// =============================================================================
// Case 9: collect_closed_panels() does not fire for a window that was never
//         asked to close (no native X press), and is a clean no-op when the
//         adapter is empty. A just-opened, never-closed window must remain
//         managed after the harvest.
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, CollectClosedPanelsDoesNotFireWithoutClose)
{
    w::NativeWindowAdapter adapter;

    // Empty-adapter harvest is a clean no-op.
    int callbacks_empty = 0;
    adapter.collect_closed_panels([&](std::string_view) { ++callbacks_empty; });
    EXPECT_EQ(callbacks_empty, 0);

    // Open a panel, then harvest: the window has not been asked to close
    // (stub path: should_close() is always false; real Win32 path: no X was
    // pressed in a headless test), so the callback must not fire and the
    // panel stays managed.
    static_cast<void>(adapter.open(make_pw("Outliner")));
    ASSERT_TRUE(adapter.is_managed("Outliner"));

    int callbacks = 0;
    adapter.collect_closed_panels([&](std::string_view id) {
        ++callbacks;
        EXPECT_EQ(id, "Outliner");
    });

    EXPECT_EQ(callbacks, 0)
        << "collect_closed_panels fired for a window that was never closed.";
    EXPECT_TRUE(adapter.is_managed("Outliner"));
}

// =============================================================================
// CROSS-PLATFORM SEAL
// =============================================================================
// X11/Cocoa backends are out-of-scope for this library (multi-week effort that
// belongs to cd::platform, not cd::ui_widgets). The adapter's stub / gap path
// already handles non-Win32 correctly: open() returns false, a NativeWindowGap
// is recorded, and Sprint-1 floating-internal rendering takes over transparently.
//
// The tests below seal this documented gap by verifying the stub path contracts
// on non-Win32 (or headless CI) and confirming ManagedWindow accessors on stubs.
// =============================================================================

// =============================================================================
// Case 10: On the stub path, ManagedWindow::width/height return 0 (no real HWND).
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, StubManagedWindowSizesAreZero)
{
    w::NativeWindowAdapter adapter;
    static_cast<void>(adapter.open(make_pw("Debug")));
    ASSERT_EQ(adapter.managed_windows().size(), 1U);

    const auto& mw = adapter.managed_windows()[0];
    if (mw.is_stub)
    {
        // Stub path: no real OS window -> size 0.
        EXPECT_EQ(mw.width(),  0U);
        EXPECT_EQ(mw.height(), 0U);
        EXPECT_FALSE(mw.should_close());
    }
    else
    {
        // Real Win32 path: size comes from the creation parameters.
        // We only assert they are non-zero to avoid brittle WM-dependent values.
        EXPECT_GT(mw.width(),  0U);
        EXPECT_GT(mw.height(), 0U);
    }
}

// =============================================================================
// Case 11: Multiple open + close round-trips leave the managed set empty.
// =============================================================================
TEST(UiWidgetsNativeWindowAdapter, MultipleOpenCloseLeavesManagedSetEmpty)
{
    w::NativeWindowAdapter adapter;

    static_cast<void>(adapter.open(make_pw("P0")));
    static_cast<void>(adapter.open(make_pw("P1")));
    static_cast<void>(adapter.open(make_pw("P2")));
    ASSERT_EQ(adapter.managed_windows().size(), 3U);

    adapter.close("P1");
    EXPECT_EQ(adapter.managed_windows().size(), 2U);
    EXPECT_FALSE(adapter.is_managed("P1"));
    EXPECT_TRUE(adapter.is_managed("P0"));
    EXPECT_TRUE(adapter.is_managed("P2"));

    adapter.close("P0");
    adapter.close("P2");
    EXPECT_TRUE(adapter.managed_windows().empty());
}
