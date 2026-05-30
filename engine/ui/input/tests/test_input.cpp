// =============================================================================
// CHROMODYNAMIC — cd::ui::input HitTester + FocusManager tests
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
// =============================================================================
#include <cd/ui/input/Input.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

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
