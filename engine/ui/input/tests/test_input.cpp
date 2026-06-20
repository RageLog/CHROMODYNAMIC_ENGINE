// =============================================================================
// CHROMODYNAMIC — cd::ui::input HitTester + FocusManager + GestureRecognizer
//
// Phase 2.1 coverage per ADR-20260530-ui-widget-library:
//   * HitTester finds topmost rect on overlap (z-order: last-wins)
//   * HitTester no-hit returns kInvalidWidget
//   * HitTester rejects invalid ids / zero-area rects
//   * FocusManager next() cycles through the chain and wraps
//   * FocusManager prev() reverses and wraps
//   * Modal capture restricts cycling to the modal sub-chain
//   * Modal pop restores prior focus state
//   * Tab navigation starting from the middle of the chain
//   * Empty focus chain is a safe no-op
//   * set_chain wholesale replace drops stale focus
//   * MouseEvent / KeyEvent POD smoke test
//
// Phase 2.2 coverage — GestureRecognizer:
//   * Two clicks within 350 ms -> kDoubleClick fires exactly once
//   * Two clicks outside 350 ms -> two separate clicks, no double-click
//   * Press + hold >= 500 ms -> kLongPress
//   * Press + move > 4 px -> kDrag fires (and no kLongPress fires)
//   * Two-pointer move-apart -> kPinchOut
//   * Quick directional swipe with velocity > min -> kSwipe* correct direction
//   * Empty event stream produces no gestures
//   * Multiple recognizers on same input behave independently
//
// Phase 2.3 coverage — depth / edge / negative / boundary (21 new tests):
//   HitTester:
//     * OutsideAllRects — point clearly off all rects returns kInvalidWidget
//     * ZOrderTieExact  — two identical rects, last entry wins
//     * NestedContainment — three fully-nested rects, deepest z-wins
//     * NegativeCoordRects — rects with negative x/y origins
//     * SinglePixelBoundary — inclusion at (x,y), exclusion at (x+w, y+h)
//   FocusManager:
//     * BlurDoesNotPopModal — blur() keeps modal stack intact
//     * PopModalOnEmptyStack — safe no-op, returns 0
//     * ClearResetsEverything — clear() wipes chain + modal + focus
//     * FocusRemovedWidget — widget removed via set_chain loses focus
//     * RegisterWidgetInvalidId — invalid id silently ignored
//     * ModalNoSubchainTabPinnedToModal — next/prev stay on modal id
//     * SetChainPreservesValidFocus — focus kept when id stays in new chain
//     * DoubleModalPushPop — depth 2 unwind restores original focus
//   GestureRecognizer:
//     * DragThresholdBoundaryJustUnder — move < threshold fires no drag
//     * LongPressAtExactBoundary — elapsed exactly >= threshold fires kLongPress
//     * DoubleClickAtExactBoundary — elapsed exactly <= window fires kDoubleClick
//     * PinchInTwoPointersMoveCloser — dist shrinks -> kPinchIn + scale < 1
//     * SwipeLeft — fast move leftward -> kSwipeLeft
//     * SwipeDown — fast downward move -> kSwipeDown
//     * SlowReleaseAfterDragNoSwipe — velocity below threshold: no kSwipe*
//     * ResetMidGestureNullifies — reset during drag prevents swipe on release
// =============================================================================
#include <cd/ui/input/Input.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace ui = cd::ui::input;

namespace
{

constexpr ui::WidgetId Id(std::uint32_t v) noexcept { return ui::WidgetId { v }; }

}  // namespace

// ---- HitTester -------------------------------------------------------------

TEST(UiInputHitTester, FindsTopmostRectOnOverlap)
{
    const std::array<ui::HitRect, 3> rects {{
        // Back layer covers the full window.
        ui::HitRect { Id(1), 0.0F,  0.0F,  200.0F, 200.0F },
        // Middle layer overlaps part of the back.
        ui::HitRect { Id(2), 50.0F, 50.0F, 100.0F, 100.0F },
        // Top layer is a small button on the middle.
        ui::HitRect { Id(3), 60.0F, 60.0F,  40.0F,  40.0F },
    }};

    EXPECT_EQ(ui::HitTester::hit_test(rects, 70.0F, 70.0F),   Id(3));   // topmost
    EXPECT_EQ(ui::HitTester::hit_test(rects, 110.0F, 110.0F), Id(2));   // mid only
    EXPECT_EQ(ui::HitTester::hit_test(rects, 10.0F, 10.0F),   Id(1));   // back only
}

TEST(UiInputHitTester, NoHitReturnsInvalid)
{
    const std::array<ui::HitRect, 2> rects {{
        ui::HitRect { Id(1), 0.0F, 0.0F, 10.0F, 10.0F },
        ui::HitRect { Id(2), 50.0F, 50.0F, 10.0F, 10.0F },
    }};

    EXPECT_EQ(ui::HitTester::hit_test(rects, 100.0F, 100.0F), ui::kInvalidWidget);
    EXPECT_FALSE(ui::HitTester::hit_test(rects, 100.0F, 100.0F).is_valid());

    // Boundary: right / bottom edges are exclusive.
    EXPECT_EQ(ui::HitTester::hit_test(rects, 10.0F, 5.0F), ui::kInvalidWidget);

    // Empty list returns invalid.
    EXPECT_EQ(ui::HitTester::hit_test(std::span<const ui::HitRect> {}, 0.0F, 0.0F),
              ui::kInvalidWidget);
}

TEST(UiInputHitTester, RejectsInvalidIdsAndZeroAreaRects)
{
    const std::array<ui::HitRect, 3> rects {{
        ui::HitRect { ui::kInvalidWidget, 0.0F, 0.0F, 100.0F, 100.0F }, // skipped
        ui::HitRect { Id(7),               0.0F, 0.0F,   0.0F,  50.0F }, // zero W
        ui::HitRect { Id(9),               0.0F, 0.0F,  50.0F,   0.0F }, // zero H
    }};
    EXPECT_EQ(ui::HitTester::hit_test(rects, 10.0F, 10.0F), ui::kInvalidWidget);
}

// ---- FocusManager: next / prev cycling ------------------------------------

TEST(UiInputFocusManager, FocusNextCyclesThroughChain)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    fm.register_widget(Id(2));
    fm.register_widget(Id(3));

    // Starting from no focus, next lands on the first.
    EXPECT_EQ(fm.next(), Id(1));
    EXPECT_EQ(fm.focused(), Id(1));

    EXPECT_EQ(fm.next(), Id(2));
    EXPECT_EQ(fm.next(), Id(3));

    // Wrap-around.
    EXPECT_EQ(fm.next(), Id(1));
    EXPECT_EQ(fm.next(), Id(2));
}

TEST(UiInputFocusManager, FocusPrevReversesAndWraps)
{
    ui::FocusManager fm;
    fm.register_widget(Id(10));
    fm.register_widget(Id(20));
    fm.register_widget(Id(30));

    // From no focus, prev lands on the last (shift-tab from "outside").
    EXPECT_EQ(fm.prev(), Id(30));
    EXPECT_EQ(fm.prev(), Id(20));
    EXPECT_EQ(fm.prev(), Id(10));

    // Wrap-around.
    EXPECT_EQ(fm.prev(), Id(30));
}

TEST(UiInputFocusManager, TabFromMiddleOfChain)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    fm.register_widget(Id(2));
    fm.register_widget(Id(3));
    fm.register_widget(Id(4));

    // Jump focus into the middle, then cycle forwards.
    ASSERT_TRUE(fm.focus(Id(2)));
    EXPECT_EQ(fm.focused(), Id(2));
    EXPECT_EQ(fm.next(), Id(3));
    EXPECT_EQ(fm.next(), Id(4));
    EXPECT_EQ(fm.next(), Id(1));  // wrap

    // Reverse from the middle.
    ASSERT_TRUE(fm.focus(Id(3)));
    EXPECT_EQ(fm.prev(), Id(2));
    EXPECT_EQ(fm.prev(), Id(1));
    EXPECT_EQ(fm.prev(), Id(4));  // wrap
}

// ---- FocusManager: empty chain --------------------------------------------

TEST(UiInputFocusManager, EmptyFocusChainHandled)
{
    ui::FocusManager fm;

    EXPECT_EQ(fm.chain_size(), 0U);
    EXPECT_EQ(fm.focused(), ui::kInvalidWidget);

    EXPECT_EQ(fm.next(), ui::kInvalidWidget);
    EXPECT_EQ(fm.prev(), ui::kInvalidWidget);
    EXPECT_EQ(fm.focused(), ui::kInvalidWidget);

    // focus() on empty chain is a silent no-op (returns false).
    EXPECT_FALSE(fm.focus(Id(1)));
    EXPECT_EQ(fm.focused(), ui::kInvalidWidget);
}

// ---- FocusManager: set_chain replace --------------------------------------

TEST(UiInputFocusManager, SetChainDropsStaleFocus)
{
    ui::FocusManager fm;
    const std::array<ui::WidgetId, 3> first  {{ Id(1), Id(2), Id(3) }};
    const std::array<ui::WidgetId, 2> second {{ Id(7), Id(8) }};

    fm.set_chain(first);
    ASSERT_TRUE(fm.focus(Id(2)));
    EXPECT_EQ(fm.focused(), Id(2));

    // Replace with a chain that no longer contains Id(2) -> focus dropped.
    fm.set_chain(second);
    EXPECT_EQ(fm.focused(), ui::kInvalidWidget);
    EXPECT_EQ(fm.chain_size(), 2U);

    // Can re-focus into the new chain.
    EXPECT_TRUE(fm.focus(Id(7)));
    EXPECT_EQ(fm.focused(), Id(7));
}

// ---- FocusManager: modal capture ------------------------------------------

TEST(UiInputFocusManager, ModalCaptureBlocksUnderneath)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));   // background widget
    fm.register_widget(Id(2));   // background widget
    fm.register_widget(Id(99));  // modal id (also lives in chain)
    fm.register_widget(Id(101)); // modal child A
    fm.register_widget(Id(102)); // modal child B

    ASSERT_TRUE(fm.focus(Id(1)));
    EXPECT_EQ(fm.focused(), Id(1));

    // Push modal -- focus jumps to modal id, prior focus saved.
    const std::size_t depth = fm.push_modal(Id(99));
    EXPECT_EQ(depth, 1U);
    EXPECT_EQ(fm.modal_top(), Id(99));
    EXPECT_EQ(fm.focused(), Id(99));

    // Register the modal sub-chain (modal id + its two children).
    const std::array<ui::WidgetId, 3> sub {{ Id(99), Id(101), Id(102) }};
    fm.register_modal_subchain(sub);

    // Background widgets are NOT in the active chain any more.
    EXPECT_FALSE(fm.is_in_active_chain(Id(1)));
    EXPECT_FALSE(fm.is_in_active_chain(Id(2)));
    EXPECT_TRUE (fm.is_in_active_chain(Id(99)));
    EXPECT_TRUE (fm.is_in_active_chain(Id(101)));

    // Attempting to focus a background widget is a silent no-op.
    EXPECT_FALSE(fm.focus(Id(1)));
    EXPECT_EQ(fm.focused(), Id(99));

    // Cycling stays inside the modal sub-chain.
    EXPECT_EQ(fm.next(), Id(101));
    EXPECT_EQ(fm.next(), Id(102));
    EXPECT_EQ(fm.next(), Id(99));   // wrap inside modal
    EXPECT_EQ(fm.prev(), Id(102));  // wrap reverse
}

TEST(UiInputFocusManager, ModalPopRestoresPriorFocus)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    fm.register_widget(Id(2));
    fm.register_widget(Id(99));

    ASSERT_TRUE(fm.focus(Id(2)));
    EXPECT_EQ(fm.focused(), Id(2));

    fm.push_modal(Id(99));
    EXPECT_EQ(fm.modal_depth(), 1U);
    EXPECT_EQ(fm.focused(), Id(99));

    // Move focus inside modal (no sub-chain registered: only Id(99) is
    // reachable, so next/prev are pinned to Id(99)).
    EXPECT_EQ(fm.next(), Id(99));
    EXPECT_EQ(fm.prev(), Id(99));

    // Pop -- prior focus (Id(2)) restored.
    const std::size_t depth_after = fm.pop_modal();
    EXPECT_EQ(depth_after, 0U);
    EXPECT_EQ(fm.modal_depth(), 0U);
    EXPECT_EQ(fm.focused(), Id(2));

    // Underneath widgets are reachable again.
    EXPECT_TRUE(fm.is_in_active_chain(Id(1)));
    EXPECT_EQ(fm.next(), Id(99));   // continues from Id(2) -> Id(99)
}

TEST(UiInputFocusManager, NestedModalsStack)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    fm.register_widget(Id(2));
    fm.register_widget(Id(50));   // outer modal id
    fm.register_widget(Id(51));   // outer modal child
    fm.register_widget(Id(60));   // inner modal id

    ASSERT_TRUE(fm.focus(Id(2)));

    fm.push_modal(Id(50));
    const std::array<ui::WidgetId, 2> outer_sub {{ Id(50), Id(51) }};
    fm.register_modal_subchain(outer_sub);
    EXPECT_EQ(fm.modal_depth(), 1U);
    EXPECT_TRUE(fm.is_in_active_chain(Id(51)));

    fm.push_modal(Id(60));
    EXPECT_EQ(fm.modal_depth(), 2U);
    EXPECT_EQ(fm.modal_top(), Id(60));
    // Outer modal's children are now also blocked.
    EXPECT_FALSE(fm.is_in_active_chain(Id(51)));
    EXPECT_TRUE (fm.is_in_active_chain(Id(60)));

    // Pop inner -- outer modal restored.
    fm.pop_modal();
    EXPECT_EQ(fm.modal_depth(), 1U);
    EXPECT_EQ(fm.modal_top(), Id(50));
    EXPECT_TRUE(fm.is_in_active_chain(Id(51)));

    // Pop outer -- back to the global chain.
    fm.pop_modal();
    EXPECT_EQ(fm.modal_depth(), 0U);
    EXPECT_TRUE(fm.is_in_active_chain(Id(1)));
}

// ---- Event POD smoke -------------------------------------------------------

TEST(UiInputEvents, MouseAndKeyPodWireTypes)
{
    const ui::MouseEvent me { 12.0F, 34.0F, ui::MouseButton::kRight, ui::MouseAction::kPress };
    EXPECT_FLOAT_EQ(me.x, 12.0F);
    EXPECT_FLOAT_EQ(me.y, 34.0F);
    EXPECT_EQ(me.button, ui::MouseButton::kRight);
    EXPECT_EQ(me.action, ui::MouseAction::kPress);

    const ui::KeyEvent ke {
        0x0009U,  // Tab keycode (placeholder; semantic mapping is a frontend concern)
        ui::KeyAction::kPress,
        static_cast<std::uint16_t>(ui::key_mods::kShift | ui::key_mods::kControl),
    };
    EXPECT_EQ(ke.keycode, 0x0009U);
    EXPECT_EQ(ke.action,  ui::KeyAction::kPress);
    EXPECT_NE(ke.modifiers & ui::key_mods::kShift,   0U);
    EXPECT_NE(ke.modifiers & ui::key_mods::kControl, 0U);
    EXPECT_EQ(ke.modifiers & ui::key_mods::kAlt,     0U);
}

// ---- GestureRecognizer helpers (local) -------------------------------------

namespace
{

/// Synthetic left-button press at (x, y).
ui::MouseEvent press_at(float x, float y) noexcept
{
    return ui::MouseEvent { x, y, ui::MouseButton::kLeft, ui::MouseAction::kPress };
}

/// Synthetic left-button release at (x, y).
ui::MouseEvent release_at(float x, float y) noexcept
{
    return ui::MouseEvent { x, y, ui::MouseButton::kLeft, ui::MouseAction::kRelease };
}

/// Synthetic mouse move at (x, y) (button field ignored for moves).
ui::MouseEvent move_to(float x, float y) noexcept
{
    return ui::MouseEvent { x, y, ui::MouseButton::kLeft, ui::MouseAction::kMove };
}

/// Synthetic right-button press at (x, y) (used as second pointer for pinch).
ui::MouseEvent rpress_at(float x, float y) noexcept
{
    return ui::MouseEvent { x, y, ui::MouseButton::kRight, ui::MouseAction::kPress };
}

/// Synthetic right-button move to (x, y).
ui::MouseEvent rmove_to(float x, float y) noexcept
{
    return ui::MouseEvent { x, y, ui::MouseButton::kRight, ui::MouseAction::kMove };
}

}  // namespace

// ---- GestureRecognizer: double-click ----------------------------------------

/// Two clicks within the 350 ms window -> exactly one kDoubleClick event.
TEST(UiInputGestureRecognizer, DoubleClickWithinWindowFiresOnce)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    // Click 1 at t=0.0 s.
    gr.feed(press_at(100.0F, 100.0F),   0.000);
    gr.feed(release_at(100.0F, 100.0F), 0.010);

    // Click 2 at t=0.2 s (well within 350 ms).
    gr.feed(press_at(100.0F, 100.0F),   0.200);
    gr.feed(release_at(100.0F, 100.0F), 0.210);

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, ui::GestureKind::kDoubleClick);
}

/// Two clicks with more than 350 ms between them -> no double-click, just two
/// separate single-click records (no gesture callback fired for plain clicks).
TEST(UiInputGestureRecognizer, DoubleClickOutsideWindowNoDoubleClick)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    // Click 1.
    gr.feed(press_at(50.0F, 50.0F),   0.000);
    gr.feed(release_at(50.0F, 50.0F), 0.010);

    // Click 2 at t=0.5 s (> 350 ms -> outside window).
    gr.feed(press_at(50.0F, 50.0F),   0.500);
    gr.feed(release_at(50.0F, 50.0F), 0.510);

    // No gesture fires for plain clicks or out-of-window second click.
    EXPECT_TRUE(events.empty());
}

// ---- GestureRecognizer: long-press ------------------------------------------

/// Press and hold for >= 500 ms without moving -> kLongPress on release.
TEST(UiInputGestureRecognizer, LongPressAfterHold)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    gr.feed(press_at(200.0F, 200.0F),   0.000);
    // Release after 600 ms (well past 500 ms threshold).
    gr.feed(release_at(200.0F, 200.0F), 0.600);

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, ui::GestureKind::kLongPress);
}

// ---- GestureRecognizer: drag (and NOT long-press) ---------------------------

/// Press then move more than 4 px -> kDrag fires; kLongPress must NOT fire
/// even if elapsed time would exceed the long-press threshold.
TEST(UiInputGestureRecognizer, DragSuppressesLongPress)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    gr.feed(press_at(10.0F, 10.0F), 0.000);
    // Move 10 px to the right -> exceeds drag_threshold_px=4.
    gr.feed(move_to(20.0F, 10.0F),  0.001);
    // Release after 600 ms -> long-press threshold would be met, but drag wins.
    gr.feed(release_at(20.0F, 10.0F), 0.600);

    // Exactly one gesture: kDrag (fired on first threshold-crossing move).
    // kLongPress must be absent.
    ASSERT_GE(events.size(), 1U);
    bool has_drag       = false;
    bool has_long_press = false;
    for (const auto& e : events)
    {
        if (e.kind == ui::GestureKind::kDrag)      { has_drag       = true; }
        if (e.kind == ui::GestureKind::kLongPress)  { has_long_press = true; }
    }
    EXPECT_TRUE(has_drag);
    EXPECT_FALSE(has_long_press);
}

// ---- GestureRecognizer: pinch-out -------------------------------------------

/// Two pointers moving apart by more than drag_threshold_px -> kPinchOut.
TEST(UiInputGestureRecognizer, TwoPointerMoveApartIsPinchOut)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    // Left pointer at x=100, right pointer at x=200 (initial dist = 100 px).
    gr.feed(press_at(100.0F, 150.0F),  0.000);
    gr.feed(rpress_at(200.0F, 150.0F), 0.001);

    // Move right pointer further right to x=220 (dist becomes 120 px, diff=20 > 4).
    gr.feed(rmove_to(220.0F, 150.0F), 0.050);

    ASSERT_FALSE(events.empty());
    EXPECT_EQ(events.back().kind, ui::GestureKind::kPinchOut);
    EXPECT_GT(events.back().scale, 1.0F);
}

// ---- GestureRecognizer: swipe direction -------------------------------------

/// A fast horizontal swipe to the right -> kSwipeRight.
TEST(UiInputGestureRecognizer, FastHorizontalSwipeRight)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    // Press at x=0, release at x=100 after 0.1 s -> velocity = 1000 px/s.
    gr.feed(press_at(0.0F, 100.0F),    0.000);
    gr.feed(move_to(100.0F, 100.0F),   0.050);  // exceeds drag threshold
    gr.feed(release_at(100.0F, 100.0F), 0.100);

    // Must have at least the swipe event.
    bool found_swipe = false;
    for (const auto& e : events)
    {
        if (e.kind == ui::GestureKind::kSwipeRight)
        {
            found_swipe = true;
        }
    }
    EXPECT_TRUE(found_swipe);
}

/// A fast upward swipe (negative y) -> kSwipeUp.
TEST(UiInputGestureRecognizer, FastVerticalSwipeUp)
{
    // Coordinate convention: y increases downward (screen space).
    // A move from y=200 to y=0 means delta.y = -200 -> kSwipeUp.
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    gr.feed(press_at(100.0F, 200.0F),   0.000);
    gr.feed(move_to(100.0F, 100.0F),    0.050);
    gr.feed(release_at(100.0F, 0.0F),   0.100);

    bool found = false;
    for (const auto& e : events)
    {
        if (e.kind == ui::GestureKind::kSwipeUp) { found = true; }
    }
    EXPECT_TRUE(found);
}

// ---- GestureRecognizer: empty event stream ----------------------------------

/// No events fed -> no gesture callback ever fires.
TEST(UiInputGestureRecognizer, EmptyEventStreamProducesNoGestures)
{
    ui::GestureRecognizer gr;
    bool fired = false;
    gr.on_gesture([&](const ui::GestureEvent&) { fired = true; });

    // Feed only a key event (currently a no-op) and nothing else.
    const ui::KeyEvent ke { 0U, ui::KeyAction::kPress, 0U };
    gr.feed(ke, 0.0);

    EXPECT_FALSE(fired);
}

// ---- GestureRecognizer: independence of multiple instances ------------------

/// Two recognizers attached to the same event stream behave independently:
/// each fires its own callback, counts its own gestures, and their state
/// machines are fully decoupled.
TEST(UiInputGestureRecognizer, MultipleRecognizersAreIndependent)
{
    ui::GestureRecognizer gr_a;
    ui::GestureRecognizer gr_b;

    std::vector<ui::GestureEvent> events_a;
    std::vector<ui::GestureEvent> events_b;

    gr_a.on_gesture([&](const ui::GestureEvent& e) { events_a.push_back(e); });
    gr_b.on_gesture([&](const ui::GestureEvent& e) { events_b.push_back(e); });

    // Feed both recognizers the same double-click sequence.
    const auto feed_both = [&](const ui::MouseEvent& ev, double t)
    {
        gr_a.feed(ev, t);
        gr_b.feed(ev, t);
    };

    feed_both(press_at(50.0F, 50.0F),   0.000);
    feed_both(release_at(50.0F, 50.0F), 0.010);
    feed_both(press_at(50.0F, 50.0F),   0.200);
    feed_both(release_at(50.0F, 50.0F), 0.210);

    // Both should have received exactly one kDoubleClick.
    ASSERT_EQ(events_a.size(), 1U);
    EXPECT_EQ(events_a[0].kind, ui::GestureKind::kDoubleClick);

    ASSERT_EQ(events_b.size(), 1U);
    EXPECT_EQ(events_b[0].kind, ui::GestureKind::kDoubleClick);

    // Reset gr_a; gr_b should be unaffected.
    gr_a.reset();

    // Another double-click -- only gr_b still fires (gr_a state was wiped).
    // But gr_a callback is still installed, so if gr_a's double-click fires
    // it would push to events_a.  After reset, last_click_valid_ is false,
    // so the first click of the new pair is just recorded, not emitted.
    feed_both(press_at(50.0F, 50.0F),   0.400);
    feed_both(release_at(50.0F, 50.0F), 0.410);
    feed_both(press_at(50.0F, 50.0F),   0.600);
    feed_both(release_at(50.0F, 50.0F), 0.610);

    // gr_b accumulated another double-click (total 2).
    EXPECT_EQ(events_b.size(), 2U);

    // gr_a: reset cleared state but preserved the callback. The fresh
    // double-click sequence (t=0.4->0.6 s, gap=200 ms < 350 ms) fires once
    // more -> total 2 events in events_a, proving the recognizer is still
    // independently functional after reset.
    EXPECT_EQ(events_a.size(), 2U);

    // Crucially: the two instances' event lists are separate objects --
    // gr_a's reset had zero effect on gr_b's accumulated state.
    EXPECT_NE(events_a.data(), events_b.data());
}

// =============================================================================
// Phase 2.3 — HitTester: edge / negative / boundary
// =============================================================================

/// Point clearly outside every rect returns kInvalidWidget even with a large
/// list present (guards the common "off-screen click" code path).
TEST(UiInputHitTester, OutsideAllRects)
{
    const std::array<ui::HitRect, 3> rects {{
        ui::HitRect { Id(1), 0.0F,   0.0F,  50.0F,  50.0F },
        ui::HitRect { Id(2), 60.0F,  60.0F, 50.0F,  50.0F },
        ui::HitRect { Id(3), 200.0F, 0.0F,  100.0F, 100.0F },
    }};

    // Point in the gap between all rects.
    EXPECT_EQ(ui::HitTester::hit_test(rects, 55.0F, 55.0F), ui::kInvalidWidget);
    // Point far to the right.
    EXPECT_EQ(ui::HitTester::hit_test(rects, 400.0F, 400.0F), ui::kInvalidWidget);
    // Negative-coordinate point (no rect covers negative space).
    EXPECT_EQ(ui::HitTester::hit_test(rects, -1.0F, -1.0F), ui::kInvalidWidget);
}

/// Two perfectly identical rects at the same position: the LAST one in the
/// list must win (z-order tie-break = topmost = last-in-list).
TEST(UiInputHitTester, ZOrderTieExact)
{
    const std::array<ui::HitRect, 2> rects {{
        ui::HitRect { Id(1), 0.0F, 0.0F, 100.0F, 100.0F },
        ui::HitRect { Id(2), 0.0F, 0.0F, 100.0F, 100.0F },
    }};

    // Centre of both rects: last entry (Id 2) must win.
    EXPECT_EQ(ui::HitTester::hit_test(rects, 50.0F, 50.0F), Id(2));

    // Reverse the list: now Id(1) is last and must win.
    const std::array<ui::HitRect, 2> reversed {{
        ui::HitRect { Id(2), 0.0F, 0.0F, 100.0F, 100.0F },
        ui::HitRect { Id(1), 0.0F, 0.0F, 100.0F, 100.0F },
    }};
    EXPECT_EQ(ui::HitTester::hit_test(reversed, 50.0F, 50.0F), Id(1));
}

/// Three fully-nested rects (outer → middle → inner): a point in the
/// innermost area must resolve to the innermost id.
TEST(UiInputHitTester, NestedContainment)
{
    const std::array<ui::HitRect, 3> rects {{
        ui::HitRect { Id(10), 0.0F,  0.0F,  200.0F, 200.0F },  // outer
        ui::HitRect { Id(20), 50.0F, 50.0F, 100.0F, 100.0F },  // middle
        ui::HitRect { Id(30), 80.0F, 80.0F,  40.0F,  40.0F },  // inner
    }};

    EXPECT_EQ(ui::HitTester::hit_test(rects, 90.0F, 90.0F), Id(30));  // inner
    EXPECT_EQ(ui::HitTester::hit_test(rects, 55.0F, 55.0F), Id(20));  // middle only
    EXPECT_EQ(ui::HitTester::hit_test(rects, 10.0F, 10.0F), Id(10));  // outer only
}

/// Rects with negative x/y origins are valid; a point inside them must match.
TEST(UiInputHitTester, NegativeCoordRects)
{
    const std::array<ui::HitRect, 2> rects {{
        ui::HitRect { Id(1), -50.0F, -50.0F, 100.0F, 100.0F },  // spans -50..50
        ui::HitRect { Id(2),  10.0F,  10.0F,  20.0F,  20.0F },  // positive region
    }};

    EXPECT_EQ(ui::HitTester::hit_test(rects, -10.0F, -10.0F), Id(1));  // inside neg rect
    EXPECT_EQ(ui::HitTester::hit_test(rects,  15.0F,  15.0F), Id(2));  // inside positive (topmost)
    EXPECT_EQ(ui::HitTester::hit_test(rects,  60.0F,  60.0F), ui::kInvalidWidget); // outside all
}

/// Boundary exactness: (x, y) is INSIDE; (x+w, y+h) is OUTSIDE (exclusive
/// right/bottom edge). Verifies the `x < x1` condition in the implementation.
TEST(UiInputHitTester, SinglePixelBoundary)
{
    // Rect covers [10, 20) × [30, 40) (width=10, height=10).
    const std::array<ui::HitRect, 1> rects {{
        ui::HitRect { Id(5), 10.0F, 30.0F, 10.0F, 10.0F },
    }};

    EXPECT_EQ(ui::HitTester::hit_test(rects, 10.0F, 30.0F), Id(5));   // top-left corner: IN
    EXPECT_EQ(ui::HitTester::hit_test(rects, 19.0F, 39.0F), Id(5));   // one inside the edge: IN
    EXPECT_EQ(ui::HitTester::hit_test(rects, 20.0F, 35.0F), ui::kInvalidWidget); // right edge: OUT
    EXPECT_EQ(ui::HitTester::hit_test(rects, 15.0F, 40.0F), ui::kInvalidWidget); // bottom edge: OUT
}

// =============================================================================
// Phase 2.3 — FocusManager: edge / negative / boundary
// =============================================================================

/// blur() clears focus but must NOT pop the modal stack.
TEST(UiInputFocusManager, BlurDoesNotPopModal)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    fm.register_widget(Id(99));

    fm.push_modal(Id(99));
    EXPECT_EQ(fm.modal_depth(), 1U);
    EXPECT_EQ(fm.focused(), Id(99));

    fm.blur();
    EXPECT_EQ(fm.focused(), ui::kInvalidWidget);
    EXPECT_EQ(fm.modal_depth(), 1U);   // modal stack unchanged
    EXPECT_EQ(fm.modal_top(), Id(99));
}

/// pop_modal() on an empty stack is a safe no-op (returns 0).
TEST(UiInputFocusManager, PopModalOnEmptyStack)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    ASSERT_TRUE(fm.focus(Id(1)));

    // No modal pushed: pop must silently return 0.
    const std::size_t depth = fm.pop_modal();
    EXPECT_EQ(depth, 0U);
    EXPECT_EQ(fm.modal_depth(), 0U);
    // Focus must be preserved (no side-effect on empty pop).
    EXPECT_EQ(fm.focused(), Id(1));
}

/// clear() wipes chain, modal stack, and focused in one call.
TEST(UiInputFocusManager, ClearResetsEverything)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    fm.register_widget(Id(2));
    ASSERT_TRUE(fm.focus(Id(1)));
    fm.push_modal(Id(2));

    fm.clear();

    EXPECT_EQ(fm.chain_size(), 0U);
    EXPECT_EQ(fm.modal_depth(), 0U);
    EXPECT_EQ(fm.focused(), ui::kInvalidWidget);
    // After clear, next/prev are safe no-ops.
    EXPECT_EQ(fm.next(), ui::kInvalidWidget);
    EXPECT_EQ(fm.prev(), ui::kInvalidWidget);
}

/// A widget that was focused, then removed from the chain via set_chain, must
/// lose focus (stale focus guard).
TEST(UiInputFocusManager, FocusRemovedWidgetBecomesInvalid)
{
    ui::FocusManager fm;
    const std::array<ui::WidgetId, 3> chain_a {{ Id(1), Id(2), Id(3) }};
    fm.set_chain(chain_a);
    ASSERT_TRUE(fm.focus(Id(3)));
    EXPECT_EQ(fm.focused(), Id(3));

    // Replace chain without Id(3).
    const std::array<ui::WidgetId, 2> chain_b {{ Id(1), Id(2) }};
    fm.set_chain(chain_b);

    EXPECT_EQ(fm.focused(), ui::kInvalidWidget);
    EXPECT_EQ(fm.chain_size(), 2U);
}

/// register_widget() with kInvalidWidget must be a silent no-op (chain size
/// stays the same; the id does not appear in the chain).
TEST(UiInputFocusManager, RegisterWidgetInvalidIdIgnored)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    fm.register_widget(ui::kInvalidWidget);  // must be ignored
    fm.register_widget(Id(2));

    EXPECT_EQ(fm.chain_size(), 2U);
    EXPECT_FALSE(fm.is_in_active_chain(ui::kInvalidWidget));
}

/// When a modal is pushed with no sub-chain registered, next() and prev()
/// must stay pinned to the modal id (cycling within a single-element chain).
TEST(UiInputFocusManager, ModalNoSubchainTabPinnedToModal)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    fm.register_widget(Id(2));
    fm.register_widget(Id(99));

    ASSERT_TRUE(fm.focus(Id(1)));
    fm.push_modal(Id(99));
    // No register_modal_subchain call: the only reachable node is Id(99).

    EXPECT_EQ(fm.next(), Id(99));
    EXPECT_EQ(fm.next(), Id(99));  // wraps to itself
    EXPECT_EQ(fm.prev(), Id(99));
    EXPECT_EQ(fm.prev(), Id(99));

    EXPECT_FALSE(fm.is_in_active_chain(Id(1)));
    EXPECT_FALSE(fm.is_in_active_chain(Id(2)));
}

/// set_chain must PRESERVE focus when the previously-focused id still appears
/// in the new chain.
TEST(UiInputFocusManager, SetChainPreservesValidFocus)
{
    ui::FocusManager fm;
    const std::array<ui::WidgetId, 3> chain_a {{ Id(1), Id(2), Id(3) }};
    fm.set_chain(chain_a);
    ASSERT_TRUE(fm.focus(Id(2)));

    // Replace chain; Id(2) still present.
    const std::array<ui::WidgetId, 4> chain_b {{ Id(4), Id(2), Id(5), Id(6) }};
    fm.set_chain(chain_b);

    // Focus must be preserved.
    EXPECT_EQ(fm.focused(), Id(2));
    EXPECT_EQ(fm.chain_size(), 4U);
}

/// Push two modals and then pop both; final focused must be the one that was
/// active before the first push.
TEST(UiInputFocusManager, DoubleModalPushPopRestoresOriginal)
{
    ui::FocusManager fm;
    fm.register_widget(Id(1));
    fm.register_widget(Id(2));
    fm.register_widget(Id(50));
    fm.register_widget(Id(60));

    ASSERT_TRUE(fm.focus(Id(2)));

    fm.push_modal(Id(50));
    EXPECT_EQ(fm.modal_depth(), 1U);
    EXPECT_EQ(fm.focused(), Id(50));

    fm.push_modal(Id(60));
    EXPECT_EQ(fm.modal_depth(), 2U);
    EXPECT_EQ(fm.focused(), Id(60));

    // Pop inner (prior_focused at level 2 = Id(50)).
    fm.pop_modal();
    EXPECT_EQ(fm.modal_depth(), 1U);
    EXPECT_EQ(fm.focused(), Id(50));

    // Pop outer (prior_focused at level 1 = Id(2)).
    fm.pop_modal();
    EXPECT_EQ(fm.modal_depth(), 0U);
    EXPECT_EQ(fm.focused(), Id(2));

    EXPECT_TRUE(fm.is_in_active_chain(Id(1)));
    EXPECT_TRUE(fm.is_in_active_chain(Id(2)));
}

// =============================================================================
// Phase 2.3 — GestureRecognizer: boundary / negative / cancel
// =============================================================================

/// Move displacement of EXACTLY (drag_threshold_px - epsilon) must NOT trigger
/// a drag. The threshold is a >= comparison so the pixel just under must be
/// silent.
TEST(UiInputGestureRecognizer, DragThresholdBoundaryJustUnder)
{
    ui::GestureThresholds th;
    th.drag_threshold_px = 8.0F;  // custom threshold
    ui::GestureRecognizer gr { th };

    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    gr.feed(press_at(0.0F, 0.0F), 0.000);
    // Move 7.99 px along x — strictly less than 8 px threshold.
    gr.feed(move_to(7.99F, 0.0F), 0.010);
    gr.feed(release_at(7.99F, 0.0F), 0.020);

    // No drag must have fired; no swipe (velocity huge but drag never started).
    const bool has_drag = std::ranges::any_of(events,
        [](const ui::GestureEvent& e){ return e.kind == ui::GestureKind::kDrag; });
    EXPECT_FALSE(has_drag);
}

/// Press and hold for EXACTLY the long_press_window_ms threshold -> kLongPress
/// must fire (>= comparison).
TEST(UiInputGestureRecognizer, LongPressAtExactBoundary)
{
    ui::GestureThresholds th;
    th.long_press_window_ms = 400.0F;
    ui::GestureRecognizer gr { th };

    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    gr.feed(press_at(50.0F, 50.0F),   0.000);
    // Release at exactly 400 ms (0.400 s).
    gr.feed(release_at(50.0F, 50.0F), 0.400);

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, ui::GestureKind::kLongPress);
}

/// Second click at EXACTLY double_click_window_ms -> kDoubleClick must fire
/// (<= comparison).
TEST(UiInputGestureRecognizer, DoubleClickAtExactBoundary)
{
    ui::GestureThresholds th;
    th.double_click_window_ms = 300.0F;
    ui::GestureRecognizer gr { th };

    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    // Click 1 at t=0 (press→release instantaneous for simplicity).
    gr.feed(press_at(10.0F, 10.0F),   0.000);
    gr.feed(release_at(10.0F, 10.0F), 0.001);

    // Click 2 at exactly 300 ms after first release.
    gr.feed(press_at(10.0F, 10.0F),   0.301);
    gr.feed(release_at(10.0F, 10.0F), 0.302);

    // The gap between last_click_t_ (0.001) and second release is ~301 ms
    // which must be <= 300 ms window if we measure press-to-press.
    // Actual implementation measures (t_release2 - last_click_t_) in ms.
    // t=0.302 - t=0.001 = 301 ms which is > 300 ms. So test the real boundary:
    // second click exactly at window_ms from first click (t=0.001 + 0.300 = 0.301).
    // Reset and re-run with tighter timestamps.
    gr.reset();
    events.clear();

    gr.feed(press_at(10.0F, 10.0F),   0.000);
    gr.feed(release_at(10.0F, 10.0F), 0.000);  // click 1 at t=0.000
    // Second click: gap = 0.300 s = exactly 300.0 ms -> must fire.
    gr.feed(press_at(10.0F, 10.0F),   0.299);
    gr.feed(release_at(10.0F, 10.0F), 0.300);  // (0.300 - 0.000) * 1000 = 300.0 ms

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, ui::GestureKind::kDoubleClick);
}

/// Two pointers that start apart and MOVE CLOSER together past the threshold
/// must produce kPinchIn with a scale factor < 1.
TEST(UiInputGestureRecognizer, PinchInTwoPointersMoveCloser)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    // Left pointer at x=100, right pointer at x=200 (initial dist = 100 px).
    gr.feed(press_at(100.0F, 150.0F),  0.000);
    gr.feed(rpress_at(200.0F, 150.0F), 0.001);

    // Move right pointer CLOSER to x=180 (dist becomes 80 px, diff = -20 < -4 threshold).
    const ui::MouseEvent rmove {
        180.0F, 150.0F, ui::MouseButton::kRight, ui::MouseAction::kMove
    };
    gr.feed(rmove, 0.050);

    const bool has_pinch_in = std::ranges::any_of(events,
        [](const ui::GestureEvent& e){ return e.kind == ui::GestureKind::kPinchIn; });
    ASSERT_TRUE(has_pinch_in);

    // Find the first pinch-in event and verify scale < 1 (pointers moved closer).
    for (const auto& e : events)
    {
        if (e.kind == ui::GestureKind::kPinchIn)
        {
            EXPECT_LT(e.scale, 1.0F);
            break;
        }
    }
}

/// A fast leftward swipe -> kSwipeLeft.
TEST(UiInputGestureRecognizer, FastSwipeLeft)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    // Press at x=200, release at x=0 after 0.1 s -> velocity = 2000 px/s leftward.
    gr.feed(press_at(200.0F, 100.0F),  0.000);
    gr.feed(move_to(100.0F, 100.0F),   0.050);  // exceeds drag threshold
    gr.feed(release_at(0.0F, 100.0F),  0.100);

    const bool found = std::ranges::any_of(events,
        [](const ui::GestureEvent& e){ return e.kind == ui::GestureKind::kSwipeLeft; });
    EXPECT_TRUE(found);
}

/// A fast downward swipe (positive y delta) -> kSwipeDown.
TEST(UiInputGestureRecognizer, FastSwipeDown)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    // Press at y=0, release at y=200 after 0.1 s (screen-space y grows down).
    gr.feed(press_at(100.0F, 0.0F),    0.000);
    gr.feed(move_to(100.0F, 100.0F),   0.050);  // exceeds drag threshold
    gr.feed(release_at(100.0F, 200.0F), 0.100);

    const bool found = std::ranges::any_of(events,
        [](const ui::GestureEvent& e){ return e.kind == ui::GestureKind::kSwipeDown; });
    EXPECT_TRUE(found);
}

/// A drag that ends with velocity BELOW the swipe threshold must NOT emit
/// any kSwipe* event (only kDrag was already emitted on the move).
TEST(UiInputGestureRecognizer, SlowReleaseAfterDragNoSwipe)
{
    ui::GestureThresholds th;
    th.drag_threshold_px           = 4.0F;
    th.swipe_min_velocity_px_per_s = 500.0F;  // high threshold
    ui::GestureRecognizer gr { th };

    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    // Drag 10 px over 1 second -> velocity = 10 px/s, well below 500 px/s.
    gr.feed(press_at(0.0F, 0.0F),    0.000);
    gr.feed(move_to(10.0F, 0.0F),    0.010);   // triggers drag
    gr.feed(release_at(10.0F, 0.0F), 1.000);   // very slow release

    const bool has_swipe = std::ranges::any_of(events,
        [](const ui::GestureEvent& e)
        {
            return e.kind == ui::GestureKind::kSwipeLeft  ||
                   e.kind == ui::GestureKind::kSwipeRight ||
                   e.kind == ui::GestureKind::kSwipeUp    ||
                   e.kind == ui::GestureKind::kSwipeDown;
        });
    EXPECT_FALSE(has_swipe);

    const bool has_drag = std::ranges::any_of(events,
        [](const ui::GestureEvent& e){ return e.kind == ui::GestureKind::kDrag; });
    EXPECT_TRUE(has_drag);  // drag still fired on the threshold-crossing move
}

/// reset() called mid-gesture (after press, before release) must nullify the
/// gesture: a subsequent release must not fire any gesture (state was wiped).
TEST(UiInputGestureRecognizer, ResetMidGestureNullifies)
{
    ui::GestureRecognizer gr;
    std::vector<ui::GestureEvent> events;
    gr.on_gesture([&](const ui::GestureEvent& e) { events.push_back(e); });

    gr.feed(press_at(0.0F, 0.0F),  0.000);
    gr.feed(move_to(50.0F, 0.0F),  0.010);   // would trigger drag

    // Wipe all state before the drag-completing release.
    gr.reset();
    events.clear();  // also clear any events emitted before reset

    // Release: pressed_ is now false, so handle_release_ bails immediately.
    gr.feed(release_at(50.0F, 0.0F), 0.020);

    EXPECT_TRUE(events.empty());
}
