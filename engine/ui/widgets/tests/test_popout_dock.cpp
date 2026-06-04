// =============================================================================
// CHROMODYNAMIC -- cd::ui::widgets::PopoutDock tests
//
// Phase 689 — Sprint-1 state-machine coverage.
//
// 8 test cases:
//   1. Default state — no detached windows.
//   2. detach_panel succeeds and window appears in detached_windows().
//   3. detach_panel same id twice returns false, count stays 1.
//   4. reattach_panel removes the window and returns true.
//   5. reattach_panel unknown id returns false.
//   6. is_detached reflects attach/detach round-trip correctly.
//   7. simulate_drag updates the position of the detached window.
//   8. simulate_drag on non-detached panel is a no-op (no crash).
// =============================================================================
#include <cd/ui/widgets/PopoutDock.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace w = cd::ui::widgets;

namespace
{

constexpr std::array<float, 2U> kPos0 { 100.0F, 200.0F };
constexpr std::array<float, 2U> kSize0 { 400.0F, 300.0F };
constexpr std::array<float, 2U> kPos1 { 800.0F, 50.0F };
constexpr std::array<float, 2U> kSize1 { 320.0F, 240.0F };
constexpr std::array<float, 2U> kDragPos { 150.0F, 250.0F };

}  // namespace

// =============================================================================
// Case 1: Default state — no detached windows.
// =============================================================================
TEST(UiWidgetsPopoutDock, DefaultStateIsEmpty)
{
    const w::PopoutDock dock;
    EXPECT_TRUE(dock.detached_windows().empty());
    EXPECT_FALSE(dock.is_detached("Inspector"));
}

// =============================================================================
// Case 2: detach_panel adds a window with correct metadata.
// =============================================================================
TEST(UiWidgetsPopoutDock, DetachPanelAddsWindow)
{
    w::PopoutDock dock;
    EXPECT_TRUE(dock.detach_panel("Inspector", kPos0, kSize0));

    const auto wins = dock.detached_windows();
    ASSERT_EQ(wins.size(), 1U);
    EXPECT_EQ(wins[0].panel_id, "Inspector");
    EXPECT_EQ(wins[0].position[0], kPos0[0]);
    EXPECT_EQ(wins[0].position[1], kPos0[1]);
    EXPECT_EQ(wins[0].size[0], kSize0[0]);
    EXPECT_EQ(wins[0].size[1], kSize0[1]);
    EXPECT_TRUE(wins[0].is_active);
    EXPECT_TRUE(dock.is_detached("Inspector"));
}

// =============================================================================
// Case 3: Detaching the same panel twice is idempotent — returns false, count stays 1.
// =============================================================================
TEST(UiWidgetsPopoutDock, DetachSamePanelTwiceIsIdempotent)
{
    w::PopoutDock dock;
    EXPECT_TRUE(dock.detach_panel("Console", kPos0, kSize0));
    EXPECT_FALSE(dock.detach_panel("Console", kPos1, kSize1));  // already detached

    EXPECT_EQ(dock.detached_windows().size(), 1U);
    // Original position must be preserved (second call is a no-op).
    EXPECT_EQ(dock.detached_windows()[0].position[0], kPos0[0]);
}

// =============================================================================
// Case 4: reattach_panel removes the window.
// =============================================================================
TEST(UiWidgetsPopoutDock, ReattachPanelRemovesWindow)
{
    w::PopoutDock dock;
    static_cast<void>(dock.detach_panel("Outliner", kPos0, kSize0));
    ASSERT_EQ(dock.detached_windows().size(), 1U);

    EXPECT_TRUE(dock.reattach_panel("Outliner"));
    EXPECT_TRUE(dock.detached_windows().empty());
    EXPECT_FALSE(dock.is_detached("Outliner"));
}

// =============================================================================
// Case 5: reattach_panel on an unknown id returns false.
// =============================================================================
TEST(UiWidgetsPopoutDock, ReattachUnknownPanelReturnsFalse)
{
    w::PopoutDock dock;
    EXPECT_FALSE(dock.reattach_panel("NonExistent"));
    // Side-effect-free: still empty.
    EXPECT_TRUE(dock.detached_windows().empty());
}

// =============================================================================
// Case 6: is_detached reflects the attach / detach round-trip for multiple panels.
// =============================================================================
TEST(UiWidgetsPopoutDock, IsDetachedReflectsRoundTrip)
{
    w::PopoutDock dock;
    static_cast<void>(dock.detach_panel("Inspector", kPos0, kSize0));
    static_cast<void>(dock.detach_panel("Console",   kPos1, kSize1));

    EXPECT_TRUE(dock.is_detached("Inspector"));
    EXPECT_TRUE(dock.is_detached("Console"));
    EXPECT_FALSE(dock.is_detached("Hierarchy"));

    static_cast<void>(dock.reattach_panel("Inspector"));
    EXPECT_FALSE(dock.is_detached("Inspector"));
    EXPECT_TRUE(dock.is_detached("Console"));
    EXPECT_EQ(dock.detached_windows().size(), 1U);
}

// =============================================================================
// Case 7: simulate_drag updates the stored position of the detached window.
// =============================================================================
TEST(UiWidgetsPopoutDock, SimulateDragUpdatesPosition)
{
    w::PopoutDock dock;
    static_cast<void>(dock.detach_panel("Inspector", kPos0, kSize0));

    dock.simulate_drag("Inspector", kDragPos);

    const auto wins = dock.detached_windows();
    ASSERT_EQ(wins.size(), 1U);
    EXPECT_EQ(wins[0].position[0], kDragPos[0]);
    EXPECT_EQ(wins[0].position[1], kDragPos[1]);
    // Size must be unchanged.
    EXPECT_EQ(wins[0].size[0], kSize0[0]);
    EXPECT_EQ(wins[0].size[1], kSize0[1]);
}

// =============================================================================
// Case 8: simulate_drag on a panel not yet detached is a no-op (no crash).
// =============================================================================
TEST(UiWidgetsPopoutDock, SimulateDragOnNonDetachedIsNoOp)
{
    w::PopoutDock dock;
    // Must not crash or modify state.
    dock.simulate_drag("GhostPanel", kDragPos);
    EXPECT_TRUE(dock.detached_windows().empty());
}
