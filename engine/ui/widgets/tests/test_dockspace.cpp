// =============================================================================
// CHROMODYNAMIC -- cd::ui::widgets::DockSpace tests
//
// T2.1 of editor enablement track. Twelve test cases (>= 10 brief minimum)
// covering the state-machine, layout, serialization, and interaction axes
// of the DockSpace primitive. All tests are CPU-only -- no GPU resources
// touched, no font loaded.
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/DockSpace.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace w = cd::ui::widgets;
namespace r = cd::ui::renderer;

namespace
{

[[nodiscard]] w::InputState make_input(float mx, float my,
                                       bool down, bool pressed, bool released,
                                       bool focused = true)
{
    w::InputState in;
    in.pointer.mouse_x       = mx;
    in.pointer.mouse_y       = my;
    in.pointer.left_down     = down;
    in.pointer.left_pressed  = pressed;
    in.pointer.left_released = released;
    in.focused               = focused;
    return in;
}

constexpr w::Rect kFullRect { 0.0F, 0.0F, 1000.0F, 800.0F };

}  // namespace

// =============================================================================
// Case 1: Default state -> root is an empty kLeaf placeholder.
// =============================================================================
TEST(UiWidgetsDockSpace, DefaultRootIsSingleLeaf)
{
    w::DockSpace ds;
    ASSERT_NE(ds.root(), nullptr);
    EXPECT_EQ(ds.root()->kind(), w::DockNodeKind::kLeaf);
    EXPECT_EQ(ds.node_count(), 1U);
    EXPECT_EQ(ds.floating_count(), 0U);
}

// =============================================================================
// Case 2: split() on a tab-group root yields a kSplit with 50/50 children.
// =============================================================================
TEST(UiWidgetsDockSpace, VerticalSplitAddsTwoChildrenWithBalancedRatio)
{
    w::DockSpace ds;
    ds.set_rect(kFullRect);

    // Seed: one tab group with panel "A".
    auto seed = w::make_single_leaf("A");
    ds.root()->set_rect(kFullRect);
    // Replace root via split() against the existing root (an empty leaf).
    ASSERT_TRUE(ds.split(ds.root(), w::DockAxis::kVertical, "B", 0.5F));

    ASSERT_NE(ds.root(), nullptr);
    EXPECT_EQ(ds.root()->kind(), w::DockNodeKind::kSplit);
    EXPECT_EQ(ds.root()->axis(), w::DockAxis::kVertical);
    EXPECT_NEAR(ds.root()->ratio(), 0.5F, 1e-5F);
    EXPECT_NE(ds.root()->first(),  nullptr);
    EXPECT_NE(ds.root()->second(), nullptr);
    EXPECT_EQ(ds.node_count(), 3U);
}

// =============================================================================
// Case 3: Splitter drag updates the parent ratio in real time.
// =============================================================================
TEST(UiWidgetsDockSpace, SplitterDragUpdatesRatio)
{
    w::DockSpace ds;
    ds.set_rect(kFullRect);

    // Build: split(A | B) vertical at 0.5
    ds.split(ds.root(), w::DockAxis::kVertical, "B", 0.5F);
    // Replace the first leaf with a single-leaf "A" tab group.
    ds.root()->first()->set_active_tab(0U);
    ds.tab_merge(ds.root()->first(), "A");

    // First tick to populate rects.
    ds.tick(make_input(0.0F, 0.0F, false, false, false));
    const float border_x = ds.root()->first()->rect().x + ds.root()->first()->rect().w;

    // Press right on the splitter band.
    ds.tick(make_input(border_x, 400.0F, true, true, false));
    // Drag halfway-right.
    ds.tick(make_input(750.0F, 400.0F, true, false, false));
    EXPECT_NEAR(ds.root()->ratio(), 0.75F, 1e-3F);

    // Release.
    ds.tick(make_input(750.0F, 400.0F, false, false, true));
}

// =============================================================================
// Case 4: TabGroup with 3 panels, click activates each.
// =============================================================================
TEST(UiWidgetsDockSpace, TabGroupClickActivatesEachPanel)
{
    w::DockSpace ds;
    ds.set_rect(kFullRect);
    // Replace root via tab_merge: empty leaf -> kTabGroup
    ds.tab_merge(ds.root(), "P0");
    ds.tab_merge(ds.root(), "P1");
    ds.tab_merge(ds.root(), "P2");

    EXPECT_EQ(ds.root()->kind(), w::DockNodeKind::kTabGroup);
    ASSERT_EQ(ds.root()->panels().size(), 3U);
    // tab_merge sets the newly added tab as active.
    EXPECT_EQ(ds.root()->active_tab(), 2U);

    ds.tick(make_input(0.0F, 0.0F, false, false, false));

    // Click on tab 0 (x ~ kTabPad/2).
    const float tab_y = ds.root()->rect().y + 4.0F;
    const float tab1_x = ds.root()->rect().x +
                         (w::TabStrip::kTabWidth + w::TabStrip::kTabPad) * 1.0F + 2.0F;
    const float tab2_x = ds.root()->rect().x +
                         (w::TabStrip::kTabWidth + w::TabStrip::kTabPad) * 2.0F + 2.0F;
    const float tab0_x = ds.root()->rect().x + 2.0F;

    ds.tick(make_input(tab1_x, tab_y, true, true, false));
    ds.tick(make_input(tab1_x, tab_y, false, false, true));
    EXPECT_EQ(ds.root()->active_tab(), 1U);

    ds.tick(make_input(tab0_x, tab_y, true, true, false));
    ds.tick(make_input(tab0_x, tab_y, false, false, true));
    EXPECT_EQ(ds.root()->active_tab(), 0U);

    ds.tick(make_input(tab2_x, tab_y, true, true, false));
    ds.tick(make_input(tab2_x, tab_y, false, false, true));
    EXPECT_EQ(ds.root()->active_tab(), 2U);
}

// =============================================================================
// Case 5: Drag a panel OUT of its tab strip (no leaf under release) ->
// floating leaf appears.
// =============================================================================
TEST(UiWidgetsDockSpace, DragPanelOutCreatesFloatingLeaf)
{
    w::DockSpace ds;
    ds.set_rect(kFullRect);
    ds.tab_merge(ds.root(), "X");
    ds.tab_merge(ds.root(), "Y");

    EXPECT_EQ(ds.floating_count(), 0U);
    EXPECT_EQ(ds.root()->panels().size(), 2U);

    ds.tick(make_input(0.0F, 0.0F, false, false, false));
    // Press on tab 0 (panel "X").
    const float tab0_x = ds.root()->rect().x + 2.0F;
    const float tab_y  = ds.root()->rect().y + 4.0F;
    ds.tick(make_input(tab0_x, tab_y, true, true, false));
    EXPECT_EQ(ds.dragging_panel(), "X");

    // Move outside the dockspace rect entirely (no leaf hit).
    ds.tick(make_input(2000.0F, 2000.0F, true, false, false));
    // Release.
    ds.tick(make_input(2000.0F, 2000.0F, false, false, true));

    EXPECT_EQ(ds.floating_count(), 1U);
    EXPECT_EQ(ds.root()->panels().size(), 1U);
    EXPECT_EQ(ds.root()->panels()[0], "Y");
}

// =============================================================================
// Case 6: Drag floating leaf onto another leaf's TOP drop zone -> horizontal
// split is created.
// =============================================================================
TEST(UiWidgetsDockSpace, DropFloatingOnTopZoneCreatesHorizontalSplit)
{
    w::DockSpace ds;
    ds.set_rect(kFullRect);
    ds.tab_merge(ds.root(), "Main");

    // Force a floating leaf via the programmatic undock + a re-seed:
    // first tab_merge "Side", then undock.
    ds.tab_merge(ds.root(), "Side");
    ASSERT_TRUE(ds.undock("Side"));
    EXPECT_EQ(ds.floating_count(), 1U);

    // Place a leaf to drop onto: root is now just "Main".
    auto* target = ds.root();
    ASSERT_TRUE(ds.drop_floating(target, w::DockDropZone::kTop));
    EXPECT_EQ(ds.floating_count(), 0U);
    EXPECT_EQ(ds.root()->kind(), w::DockNodeKind::kSplit);
    EXPECT_EQ(ds.root()->axis(), w::DockAxis::kHorizontal);
}

// =============================================================================
// Case 7: serialize() -> bytes -> restore() reproduces identical tree.
// =============================================================================
TEST(UiWidgetsDockSpace, SerializeRoundTripPreservesTree)
{
    w::DockSpace src;
    src.set_rect(kFullRect);
    src.tab_merge(src.root(), "P0");
    src.tab_merge(src.root(), "P1");
    src.split(src.root(), w::DockAxis::kVertical, "P2", 0.4F);
    // src.root is now kSplit{ kTabGroup[P0,P1] | kTabGroup[P2] }

    const std::vector<std::byte> bytes = src.serialize();
    ASSERT_FALSE(bytes.empty());

    w::DockSpace dst;
    ASSERT_TRUE(dst.restore(bytes));

    ASSERT_NE(dst.root(), nullptr);
    EXPECT_EQ(dst.root()->kind(), w::DockNodeKind::kSplit);
    EXPECT_EQ(dst.root()->axis(), w::DockAxis::kVertical);
    EXPECT_NEAR(dst.root()->ratio(), 0.4F, 1e-5F);

    const w::DockNode* a = dst.root()->first();
    const w::DockNode* b = dst.root()->second();
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(a->kind(), w::DockNodeKind::kTabGroup);
    EXPECT_EQ(b->kind(), w::DockNodeKind::kTabGroup);
    ASSERT_EQ(a->panels().size(), 2U);
    EXPECT_EQ(a->panels()[0], "P0");
    EXPECT_EQ(a->panels()[1], "P1");
    ASSERT_EQ(b->panels().size(), 1U);
    EXPECT_EQ(b->panels()[0], "P2");
}

// =============================================================================
// Case 8: restore() with empty / malformed payload is a no-op (returns false).
// =============================================================================
TEST(UiWidgetsDockSpace, RestoreEmptyOrMalformedIsNoOp)
{
    w::DockSpace ds;
    ds.tab_merge(ds.root(), "Original");
    const std::size_t orig_count = ds.node_count();

    EXPECT_FALSE(ds.restore({}));
    EXPECT_EQ(ds.node_count(), orig_count);
    ASSERT_EQ(ds.root()->panels().size(), 1U);
    EXPECT_EQ(ds.root()->panels()[0], "Original");

    // Wrong magic header.
    const std::array<std::byte, 8> bad_magic {
        std::byte { 'X' }, std::byte { 'X' }, std::byte { 'X' }, std::byte { 'X' },
        std::byte { 'X' }, std::byte { 'X' }, std::byte { 'X' }, std::byte { 'X' }
    };
    EXPECT_FALSE(ds.restore(std::span<const std::byte>(bad_magic.data(), bad_magic.size())));
    EXPECT_EQ(ds.node_count(), orig_count);
    ASSERT_EQ(ds.root()->panels().size(), 1U);
    EXPECT_EQ(ds.root()->panels()[0], "Original");
}

// =============================================================================
// Case 9: Nested splits -- inner child rect respects parent rect.
// =============================================================================
TEST(UiWidgetsDockSpace, NestedSplitsRespectParentRect)
{
    w::DockSpace ds;
    ds.set_rect(kFullRect);
    ds.tab_merge(ds.root(), "Root");                         // root = TabGroup[Root]
    ds.split(ds.root(), w::DockAxis::kVertical, "Right", 0.5F);
    // root = Split V 0.5 { TabGroup[Root] | TabGroup[Right] }
    auto* left = ds.root()->first();
    ds.split(left, w::DockAxis::kHorizontal, "BottomLeft", 0.5F);
    // left = Split H 0.5 { TabGroup[Root] | TabGroup[BottomLeft] }

    ds.tick(make_input(0.0F, 0.0F, false, false, false));

    EXPECT_NEAR(ds.root()->first()->rect().w, 500.0F, 1e-3F);
    EXPECT_NEAR(ds.root()->second()->rect().w, 500.0F, 1e-3F);
    EXPECT_NEAR(ds.root()->first()->first()->rect().h,  400.0F, 1e-3F);
    EXPECT_NEAR(ds.root()->first()->second()->rect().h, 400.0F, 1e-3F);
    EXPECT_NEAR(ds.root()->first()->first()->rect().w, 500.0F, 1e-3F);
}

// =============================================================================
// Case 10: activate_panel() via API selects the requested panel as active tab.
// =============================================================================
TEST(UiWidgetsDockSpace, ActivateInactivePanelViaApi)
{
    w::DockSpace ds;
    ds.tab_merge(ds.root(), "A");
    ds.tab_merge(ds.root(), "B");
    ds.tab_merge(ds.root(), "C");
    // tab_merge sets active to the last appended tab.
    EXPECT_EQ(ds.root()->active_tab(), 2U);

    EXPECT_TRUE(ds.activate_panel("A"));
    EXPECT_EQ(ds.root()->active_tab(), 0U);
    EXPECT_TRUE(ds.activate_panel("B"));
    EXPECT_EQ(ds.root()->active_tab(), 1U);

    // Unknown panel id returns false; active tab unchanged.
    EXPECT_FALSE(ds.activate_panel("ZZZ"));
    EXPECT_EQ(ds.root()->active_tab(), 1U);
}

// =============================================================================
// Case 11: register_panel + draw walks the registered drawer for active tab.
// =============================================================================
TEST(UiWidgetsDockSpace, RegisteredDrawerCalledForActiveTab)
{
    w::DockSpace ds;
    ds.set_rect(kFullRect);
    ds.tab_merge(ds.root(), "Foo");
    ds.tab_merge(ds.root(), "Bar");
    ds.activate_panel("Foo");

    int foo_calls = 0;
    int bar_calls = 0;
    ds.register_panel("Foo",
        [&](const w::Rect&, r::DrawBatcher&, cd::ui::font::Font*, const w::Theme&) {
            ++foo_calls;
        });
    ds.register_panel("Bar",
        [&](const w::Rect&, r::DrawBatcher&, cd::ui::font::Font*, const w::Theme&) {
            ++bar_calls;
        });

    ds.tick(make_input(0.0F, 0.0F, false, false, false));

    r::DrawBatcher batcher;
    batcher.begin_frame();
    w::Theme theme;
    ds.draw(batcher, nullptr, theme);

    EXPECT_EQ(foo_calls, 1);
    EXPECT_EQ(bar_calls, 0);

    // Switch active and re-draw.
    ds.activate_panel("Bar");
    batcher.begin_frame();
    ds.draw(batcher, nullptr, theme);
    EXPECT_EQ(foo_calls, 1);
    EXPECT_EQ(bar_calls, 1);
}

// =============================================================================
// Case 12: Drag-drop into the CENTER zone of a different leaf merges the
// floating panel into that leaf's tab group.
// =============================================================================
TEST(UiWidgetsDockSpace, DropFloatingOnCenterZoneMergesAsTab)
{
    w::DockSpace ds;
    ds.set_rect(kFullRect);
    ds.tab_merge(ds.root(), "Target");
    // Produce a floating leaf via tab_merge + undock.
    ds.tab_merge(ds.root(), "Drifter");
    ASSERT_TRUE(ds.undock("Drifter"));
    EXPECT_EQ(ds.floating_count(), 1U);

    auto* target = ds.root();
    ASSERT_TRUE(ds.drop_floating(target, w::DockDropZone::kCenter));
    EXPECT_EQ(ds.floating_count(), 0U);
    ASSERT_EQ(ds.root()->kind(), w::DockNodeKind::kTabGroup);
    ASSERT_EQ(ds.root()->panels().size(), 2U);
    EXPECT_EQ(ds.root()->panels()[0], "Target");
    EXPECT_EQ(ds.root()->panels()[1], "Drifter");
    EXPECT_EQ(ds.root()->active_tab(), 1U);
}
