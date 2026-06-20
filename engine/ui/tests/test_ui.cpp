// =============================================================================
// CHROMODYNAMIC — cd::ui tests
// =============================================================================
#include <cd/ui/Anchor.hpp>
#include <cd/ui/ProgressBar.hpp>
#include <cd/ui/Spinner.hpp>
#include <cd/ui/TabBar.hpp>
#include <cd/ui/Theme.hpp>
#include <cd/ui/Tooltip.hpp>
#include <cd/ui/Widget.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

namespace
{

TEST(UiRect, ContainsRespectsBounds)
{
    cd::ui::Rect r { 10.0F, 20.0F, 30.0F, 40.0F };
    EXPECT_TRUE(r.contains(15.0F, 25.0F));
    EXPECT_FALSE(r.contains(5.0F, 25.0F));
    EXPECT_FALSE(r.contains(15.0F, 65.0F));
    // Right/bottom edges are exclusive — standard half-open rect convention.
    EXPECT_FALSE(r.contains(40.0F, 25.0F));
}

TEST(UiWidget, RootHitTestFindsSelf)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    auto* hit = root.hit_test(50.0F, 50.0F);
    EXPECT_EQ(hit, &root);
}

TEST(UiWidget, MissOutsideBoundsReturnsNullptr)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 50.0F, 50.0F });
    EXPECT_EQ(root.hit_test(100.0F, 100.0F), nullptr);
}

TEST(UiWidget, HiddenWidgetNotHit)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    root.set_visible(false);
    EXPECT_EQ(root.hit_test(50.0F, 50.0F), nullptr);
}

TEST(UiWidget, ChildHitTestPrefersTopmost)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    auto* below = root.add_child<cd::ui::Panel>();
    below->set_bounds({ 10.0F, 10.0F, 80.0F, 80.0F });
    auto* above = root.add_child<cd::ui::Panel>();
    above->set_bounds({ 20.0F, 20.0F, 30.0F, 30.0F });
    auto* hit = root.hit_test(30.0F, 30.0F);
    EXPECT_EQ(hit, above);
}

TEST(UiButton, ClickIncrementsCounterAndFiresCallback)
{
    cd::ui::Button btn { "OK" };
    btn.set_bounds({ 0.0F, 0.0F, 60.0F, 24.0F });
    int fired { 0 };
    btn.set_on_click(
        [&]
        {
            ++fired;
        }
    );
    btn.dispatch_click(10.0F, 10.0F);
    EXPECT_EQ(btn.click_count(), 1U);
    EXPECT_EQ(fired, 1);
    btn.dispatch_click(10.0F, 10.0F);
    EXPECT_EQ(btn.click_count(), 2U);
    EXPECT_EQ(fired, 2);
}

TEST(UiButton, ClickOutsideBoundsIgnored)
{
    cd::ui::Button btn { "OK" };
    btn.set_bounds({ 0.0F, 0.0F, 60.0F, 24.0F });
    btn.dispatch_click(100.0F, 100.0F);
    EXPECT_EQ(btn.click_count(), 0U);
}

TEST(UiWidget, ClickDispatchedToChildOnly)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 200.0F, 200.0F });
    auto* btn = root.add_child<cd::ui::Button>("Press");
    btn->set_bounds({ 50.0F, 50.0F, 60.0F, 24.0F });
    int fired { 0 };
    btn->set_on_click(
        [&]
        {
            ++fired;
        }
    );
    root.dispatch_click(60.0F, 60.0F);  // inside the button
    EXPECT_EQ(fired, 1);
    root.dispatch_click(10.0F, 10.0F);  // misses the button, hits the panel
    EXPECT_EQ(fired, 1);
}

TEST(UiWidget, CollectDrawCommandsWalksTree)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    root.add_child<cd::ui::Label>("hello");
    auto* btn = root.add_child<cd::ui::Button>("press");
    btn->set_bounds({ 10.0F, 10.0F, 50.0F, 20.0F });

    std::vector<cd::ui::DrawCommand> cmds;
    root.collect_draw_commands(cmds);
    // Root panel rect + label text + button rect + button text = 4 commands.
    EXPECT_EQ(cmds.size(), 4U);
    EXPECT_EQ(cmds.front().kind, cd::ui::DrawKind::kRect);
}

TEST(UiWidget, HiddenSubtreeSkippedInDrawCollection)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    auto* hidden = root.add_child<cd::ui::Panel>();
    hidden->set_visible(false);
    hidden->add_child<cd::ui::Label>("invisible");
    std::vector<cd::ui::DrawCommand> cmds;
    root.collect_draw_commands(cmds);
    EXPECT_EQ(cmds.size(), 1U);  // only the root panel rect
}

TEST(UiWidget, AddChildReturnsObserverPointer)
{
    cd::ui::Panel root {};
    auto* label = root.add_child<cd::ui::Label>("text");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), "text");
    EXPECT_EQ(root.child_count(), 1U);
}

TEST(UiWidget, RemoveChildDetachesAndDestroys)
{
    cd::ui::Panel root {};
    auto* a = root.add_child<cd::ui::Label>("a");
    auto* b = root.add_child<cd::ui::Label>("b");
    EXPECT_EQ(root.child_count(), 2U);
    EXPECT_TRUE(root.remove_child(a));
    EXPECT_EQ(root.child_count(), 1U);
    // b should still be reachable via the children() span.
    ASSERT_FALSE(root.children().empty());
    EXPECT_EQ(root.children().front().get(), b);
    // Removing a now-stale pointer fails cleanly.
    EXPECT_FALSE(root.remove_child(a));
}

TEST(UiWidget, ClearChildrenEmptiesTreeWithoutTouchingSelf)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    root.add_child<cd::ui::Label>("x");
    root.add_child<cd::ui::Label>("y");
    root.add_child<cd::ui::Label>("z");
    EXPECT_EQ(root.child_count(), 3U);
    root.clear_children();
    EXPECT_EQ(root.child_count(), 0U);
    // Root must still be drawable and hit-testable.
    EXPECT_EQ(root.hit_test(50.0F, 50.0F), &root);
}

TEST(UiWidget, ParentRelativeChildHitTest)
{
    // Parent at (100, 100), child at (10, 10, 30, 30) relative to parent.
    // A click at screen-space (115, 115) maps to child-local (5, 5) — must
    // hit the child.
    cd::ui::Panel root {};
    root.set_bounds({ 100.0F, 100.0F, 200.0F, 200.0F });
    auto* inner = root.add_child<cd::ui::Panel>();
    inner->set_bounds({ 10.0F, 10.0F, 30.0F, 30.0F });
    EXPECT_EQ(root.hit_test(115.0F, 115.0F), inner);
}

TEST(UiWidget, ParentRelativeDrawCommandsCompose)
{
    // Confirm collect_draw_commands emits ABSOLUTE rects even when children
    // are stored with parent-relative bounds.
    cd::ui::Panel root {};
    root.set_bounds({ 100.0F, 100.0F, 200.0F, 200.0F });
    auto* inner = root.add_child<cd::ui::Panel>();
    inner->set_bounds({ 10.0F, 20.0F, 50.0F, 60.0F });
    std::vector<cd::ui::DrawCommand> cmds;
    root.collect_draw_commands(cmds);
    ASSERT_EQ(cmds.size(), 2U);
    EXPECT_FLOAT_EQ(cmds[1].rect.x, 110.0F);
    EXPECT_FLOAT_EQ(cmds[1].rect.y, 120.0F);
}

// ---- Band-3 umbrella-v1 topup: deeper widget-tree branches ----------------
//
// The existing cases cover single-level hit-test, two-level parent-relative
// hit, and topmost-of-two siblings. These add the genuinely-untested branches
// the umbrella-v1 seal rests on: 3-deep nested coordinate composition through
// hit_test, dispatch reaching the DEEPEST widget (not an ancestor), absolute
// draw-command ordering across nesting, and remove_child on a NESTED node.

// (a) 3-deep nested hit-test composes coordinate frames at every level.
//     root@(100,100) > mid@(20,20) > leaf@(5,5,10,10). A screen-space click at
//     (128,128) maps to mid-local (8,8) then leaf-local (3,3) — inside the leaf.
//     A click at (122,122) lands in mid but OUTSIDE leaf -> returns mid.
TEST(UiWidget, ThreeDeepNestedHitTestComposesFrames)
{
    cd::ui::Panel root {};
    root.set_bounds({ 100.0F, 100.0F, 200.0F, 200.0F });
    auto* mid = root.add_child<cd::ui::Panel>();
    mid->set_bounds({ 20.0F, 20.0F, 80.0F, 80.0F });
    auto* leaf = mid->add_child<cd::ui::Panel>();
    leaf->set_bounds({ 5.0F, 5.0F, 10.0F, 10.0F });

    // (128,128) -> mid-local (8,8) -> leaf-local (3,3): inside leaf.
    EXPECT_EQ(root.hit_test(128.0F, 128.0F), leaf);
    // (122,122) -> mid-local (2,2): inside mid but before leaf origin (5,5).
    EXPECT_EQ(root.hit_test(122.0F, 122.0F), mid);
    // Far corner inside root but outside mid -> returns root itself.
    EXPECT_EQ(root.hit_test(290.0F, 290.0F), &root);
}

// (b) dispatch_click routes to the DEEPEST widget under the point, firing only
//     the leaf button's handler — not an ancestor's. Confirms dispatch uses the
//     same deepest-hit result, not a shallow first-match.
TEST(UiWidget, DispatchReachesDeepestButton)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 200.0F, 200.0F });
    auto* container = root.add_child<cd::ui::Panel>();
    container->set_bounds({ 50.0F, 50.0F, 100.0F, 100.0F });
    auto* btn = container->add_child<cd::ui::Button>("Deep");
    btn->set_bounds({ 10.0F, 10.0F, 40.0F, 20.0F });

    int fired { 0 };
    btn->set_on_click([&] { ++fired; });

    // Screen (75,75) -> container-local (25,25) -> button-local (15,15): inside.
    root.dispatch_click(75.0F, 75.0F);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(btn->click_count(), 1U);

    // A click inside container but OUTSIDE the button must NOT fire the button.
    root.dispatch_click(55.0F, 55.0F);  // container-local (5,5): before button.
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(btn->click_count(), 1U);
}

// (c) Draw-command emission order is parent-before-child (painter's algorithm)
//     and absolute rects compose through 3 levels. Order matters for the
//     frontend's back-to-front blit; this pins it.
TEST(UiWidget, DrawOrderIsParentBeforeChildAcrossNesting)
{
    cd::ui::Panel root { cd::ui::Color { 1.0F, 0.0F, 0.0F, 1.0F } };
    root.set_bounds({ 10.0F, 10.0F, 100.0F, 100.0F });
    auto* mid = root.add_child<cd::ui::Panel>(cd::ui::Color { 0.0F, 1.0F, 0.0F, 1.0F });
    mid->set_bounds({ 5.0F, 5.0F, 50.0F, 50.0F });
    mid->add_child<cd::ui::Label>("leaf");  // text, drawn after mid's rect.

    std::vector<cd::ui::DrawCommand> cmds;
    root.collect_draw_commands(cmds);
    // root rect, mid rect, leaf text = 3 commands, parent first.
    ASSERT_EQ(cmds.size(), 3U);
    EXPECT_EQ(cmds[0].kind, cd::ui::DrawKind::kRect);   // root
    EXPECT_FLOAT_EQ(cmds[0].rect.x, 10.0F);
    EXPECT_EQ(cmds[1].kind, cd::ui::DrawKind::kRect);   // mid (10+5)
    EXPECT_FLOAT_EQ(cmds[1].rect.x, 15.0F);
    EXPECT_EQ(cmds[2].kind, cd::ui::DrawKind::kText);   // leaf text (10+5+0)
    EXPECT_FLOAT_EQ(cmds[2].rect.x, 15.0F);
}

// (d) remove_child detaches a NESTED child (not a direct root child). The
//     mid-level panel removes its own leaf; root's subtree shrinks accordingly
//     and the removed leaf no longer appears in draw collection.
TEST(UiWidget, RemoveNestedChildPrunesSubtree)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    auto* mid = root.add_child<cd::ui::Panel>();
    mid->set_bounds({ 0.0F, 0.0F, 80.0F, 80.0F });
    auto* leaf = mid->add_child<cd::ui::Label>("gone-soon");
    EXPECT_EQ(mid->child_count(), 1U);

    // root cannot remove a grandchild — only the direct parent can.
    EXPECT_FALSE(root.remove_child(leaf));
    EXPECT_TRUE(mid->remove_child(leaf));
    EXPECT_EQ(mid->child_count(), 0U);

    std::vector<cd::ui::DrawCommand> cmds;
    root.collect_draw_commands(cmds);
    // root rect + mid rect only; the label text is gone.
    EXPECT_EQ(cmds.size(), 2U);
    for (const auto& c : cmds)
    {
        EXPECT_EQ(c.kind, cd::ui::DrawKind::kRect);
    }
}


TEST(Anchor, StretchFillsParent)
{
    cd::ui::IntRect parent { 0, 0, 1920, 1080 };
    auto r = cd::ui::resolve(parent, cd::ui::stretch());
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.w, 1920);
    EXPECT_EQ(r.h, 1080);
}

TEST(Anchor, CenterPlacesFixedRect)
{
    cd::ui::IntRect parent { 0, 0, 800, 600 };
    auto r = cd::ui::resolve(parent, cd::ui::center(200, 100));
    EXPECT_EQ(r.w, 200);
    EXPECT_EQ(r.h, 100);
    EXPECT_EQ(r.x, 300);  // (800 - 200) / 2
    EXPECT_EQ(r.y, 250);  // (600 - 100) / 2
}

TEST(Anchor, OffsetMarginShrinksFromEdges)
{
    cd::ui::IntRect parent { 0, 0, 1000, 1000 };
    cd::ui::Anchor a { 0.0F, 0.0F, 1.0F, 1.0F, 50, 50, -50, -50 };
    auto r = cd::ui::resolve(parent, a);
    EXPECT_EQ(r.x, 50);
    EXPECT_EQ(r.y, 50);
    EXPECT_EQ(r.w, 900);
    EXPECT_EQ(r.h, 900);
}

TEST(Anchor, NestedParentOriginRespected)
{
    cd::ui::IntRect parent { 100, 200, 400, 300 };
    auto r = cd::ui::resolve(parent, cd::ui::stretch());
    EXPECT_EQ(r.x, 100);
    EXPECT_EQ(r.y, 200);
    EXPECT_EQ(r.w, 400);
    EXPECT_EQ(r.h, 300);
}



TEST(Theme, DarkThemeHasDarkBackground)
{
    const auto t = cd::ui::dark_theme();
    EXPECT_LT(t.background.r, 64);
    EXPECT_LT(t.background.g, 64);
    EXPECT_LT(t.background.b, 64);
}

TEST(Theme, LightThemeHasLightBackground)
{
    const auto t = cd::ui::light_theme();
    EXPECT_GT(t.background.r, 192);
    EXPECT_GT(t.background.g, 192);
    EXPECT_GT(t.background.b, 192);
}

TEST(Theme, SpacingTokensIncrease)
{
    const auto t = cd::ui::dark_theme();
    EXPECT_LT(t.pad_xs, t.pad_s);
    EXPECT_LT(t.pad_s,  t.pad_m);
    EXPECT_LT(t.pad_m,  t.pad_l);
    EXPECT_LT(t.pad_l,  t.pad_xl);
}

TEST(Theme, Color32Equality)
{
    cd::ui::Color32 a { 1, 2, 3, 4 };
    cd::ui::Color32 b { 1, 2, 3, 4 };
    cd::ui::Color32 c { 5, 6, 7, 8 };
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a == c);
}



TEST(Tooltip, NotVisibleBeforeDelay)
{
    cd::ui::Tooltip t;
    t.set_delay(0.5F);
    t.update(1, 0.0F);
    EXPECT_FALSE(t.visible());
    t.update(1, 0.3F);
    EXPECT_FALSE(t.visible());
}

TEST(Tooltip, VisibleAfterDelay)
{
    cd::ui::Tooltip t;
    t.set_delay(0.5F);
    t.update(1, 0.0F);
    t.update(1, 0.6F);
    EXPECT_TRUE(t.visible());
}

TEST(Tooltip, NewTargetRestartsTimer)
{
    cd::ui::Tooltip t;
    t.set_delay(0.4F);
    t.update(1, 0.0F);
    t.update(1, 0.5F);  // visible
    EXPECT_TRUE(t.visible());
    t.update(2, 0.6F);  // new target — restart
    EXPECT_FALSE(t.visible());
    EXPECT_EQ(t.target(), 2u);
}

TEST(Tooltip, ZeroTargetClears)
{
    cd::ui::Tooltip t;
    t.set_delay(0.1F);
    t.update(1, 0.0F);
    t.update(1, 0.2F);
    EXPECT_TRUE(t.visible());
    t.update(0, 0.3F);
    EXPECT_FALSE(t.visible());
    EXPECT_EQ(t.target(), 0u);
}

}  // namespace



TEST(ProgressBar, EmptyTotalReturnsZeroProgress)
{
    cd::ui::ProgressBar p;
    EXPECT_FLOAT_EQ(p.progress(), 0.0F);
    EXPECT_FALSE(p.is_complete());
}

TEST(ProgressBar, TickAdvancesProgress)
{
    cd::ui::ProgressBar p;
    p.set_total(10);
    p.tick(3);
    EXPECT_FLOAT_EQ(p.progress(), 0.3F);
    EXPECT_FALSE(p.is_complete());
}

TEST(ProgressBar, TickClampsToTotal)
{
    cd::ui::ProgressBar p;
    p.set_total(5);
    p.tick(100);
    EXPECT_EQ(p.done(), 5u);
    EXPECT_TRUE(p.is_complete());
    EXPECT_FLOAT_EQ(p.progress(), 1.0F);
}

TEST(ProgressBar, ResetReturnsToZero)
{
    cd::ui::ProgressBar p;
    p.set_total(8);
    p.tick(5);
    p.reset();
    EXPECT_EQ(p.done(), 0u);
}

TEST(ProgressBar, SetDoneClamps)
{
    cd::ui::ProgressBar p;
    p.set_total(5);
    p.set_done(99);
    EXPECT_EQ(p.done(), 5u);
}

TEST(Spinner, TickAdvancesAngle)
{
    cd::ui::Spinner s;
    s.set_speed_rps(1.0F);  // 2*pi per second
    const float before = s.angle();
    s.tick(0.25F);          // 0.25s → +pi/2
    EXPECT_GT(s.angle(), before);
}

TEST(Spinner, AngleWrapsAtTau)
{
    cd::ui::Spinner s;
    s.set_speed_rps(1.0F);
    s.tick(2.0F);           // 2 full turns → wraps
    EXPECT_GE(s.angle(), 0.0F);
    EXPECT_LT(s.angle(), 6.2832F);
}

TEST(Spinner, ResetReturnsToZero)
{
    cd::ui::Spinner s;
    s.tick(0.5F);
    s.reset();
    EXPECT_FLOAT_EQ(s.angle(), 0.0F);
}

#include <cd/ui/Toast.hpp>

TEST(ToastQueue, PushAccumulates)
{
    cd::ui::ToastQueue q;
    q.push("first", 1.0, 0.0);
    q.push("second", 1.0, 0.0);
    EXPECT_EQ(q.size(), 2u);
}

TEST(ToastQueue, UpdateEvictsExpired)
{
    cd::ui::ToastQueue q;
    q.push("short", 0.5, 0.0);
    q.push("long", 5.0, 0.0);
    q.update(1.0);
    EXPECT_EQ(q.size(), 1u);
    EXPECT_EQ(q.active()[0].message, "long");
}

TEST(ToastQueue, DismissByIdRemoves)
{
    cd::ui::ToastQueue q;
    auto id = q.push("dismiss-me", 10.0, 0.0);
    EXPECT_TRUE(q.dismiss(id));
    EXPECT_EQ(q.size(), 0u);
    EXPECT_FALSE(q.dismiss(9999));
}

TEST(ToastQueue, SeverityPropagates)
{
    cd::ui::ToastQueue q;
    q.push("err", 1.0, 0.0, cd::ui::ToastSeverity::kError);
    ASSERT_EQ(q.size(), 1u);
    EXPECT_EQ(q.active()[0].severity, cd::ui::ToastSeverity::kError);
}

TEST(TabBar, AddSetsFirstActive)
{
    cd::ui::TabBar b;
    b.add(1, "alpha");
    ASSERT_NE(b.active(), nullptr);
    EXPECT_EQ(b.active()->id, 1u);
}

TEST(TabBar, SetActiveByIdSwitches)
{
    cd::ui::TabBar b;
    b.add(1, "a");
    b.add(2, "b");
    b.add(3, "c");
    EXPECT_TRUE(b.set_active(3));
    EXPECT_EQ(b.active()->id, 3u);
    EXPECT_FALSE(b.set_active(99));
}

TEST(TabBar, NextPrevWrapsAround)
{
    cd::ui::TabBar b;
    b.add(1, "a");
    b.add(2, "b");
    b.add(3, "c");
    b.set_active(3);
    b.next();
    EXPECT_EQ(b.active()->id, 1u);
    b.prev();
    EXPECT_EQ(b.active()->id, 3u);
}

TEST(TabBar, CloseAdjustsActive)
{
    cd::ui::TabBar b;
    b.add(1, "a");
    b.add(2, "b");
    b.add(3, "c");
    b.set_active(2);
    EXPECT_TRUE(b.close(2));
    EXPECT_NE(b.active(), nullptr);
    EXPECT_EQ(b.size(), 2u);
}

#include <cd/ui/ContextMenu.hpp>

TEST(ContextMenu, NotOpenByDefault)
{
    cd::ui::ContextMenu m;
    EXPECT_FALSE(m.is_open());
}

TEST(ContextMenu, OpenSetsPositionAndFlag)
{
    cd::ui::ContextMenu m;
    m.open(100.0F, 200.0F);
    EXPECT_TRUE(m.is_open());
    EXPECT_FLOAT_EQ(m.x(), 100.0F);
    EXPECT_FLOAT_EQ(m.y(), 200.0F);
}

TEST(ContextMenu, InvokeFiresAndCloses)
{
    cd::ui::ContextMenu m;
    int hits = 0;
    m.add_item("Foo", [&] { ++hits; });
    m.open(0.0F, 0.0F);
    EXPECT_TRUE(m.invoke(0));
    EXPECT_EQ(hits, 1);
    EXPECT_FALSE(m.is_open());
}

TEST(ContextMenu, DisabledItemNotInvoked)
{
    cd::ui::ContextMenu m;
    int hits = 0;
    m.add_item("Foo", [&] { ++hits; }, /*enabled=*/false);
    EXPECT_FALSE(m.invoke(0));
    EXPECT_EQ(hits, 0);
}

TEST(ContextMenu, OutOfRangeIndexFails)
{
    cd::ui::ContextMenu m;
    EXPECT_FALSE(m.invoke(99));
}

TEST(ContextMenu, ClearItemsClosesAndEmpties)
{
    cd::ui::ContextMenu m;
    m.add_item("A", [] {});
    m.add_item("B", [] {});
    m.open(5.0F, 5.0F);
    EXPECT_EQ(m.size(), 2u);
    EXPECT_TRUE(m.is_open());
    m.clear_items();
    EXPECT_EQ(m.size(), 0u);
    EXPECT_FALSE(m.is_open());
}

TEST(ContextMenu, ItemsSpanHasCorrectLabels)
{
    cd::ui::ContextMenu m;
    m.add_item("First",  [] {});
    m.add_item("Second", [] {}, false);
    ASSERT_EQ(m.items().size(), 2u);
    EXPECT_EQ(m.items()[0].label, "First");
    EXPECT_TRUE(m.items()[0].enabled);
    EXPECT_EQ(m.items()[1].label, "Second");
    EXPECT_FALSE(m.items()[1].enabled);
}

// ---- Widget: disabled-widget hit-test skip --------------------------------

TEST(UiWidget, DisabledWidgetSkippedByHitTest)
{
    // A disabled widget that occupies the whole area must NOT be returned
    // by hit_test — the hit falls through to the parent (or nullptr).
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    auto* child = root.add_child<cd::ui::Panel>();
    child->set_bounds({ 10.0F, 10.0F, 50.0F, 50.0F });
    child->set_enabled(false);

    // The point is inside the child's bounds, but disabled → falls back to root.
    EXPECT_EQ(root.hit_test(30.0F, 30.0F), &root);
}

TEST(UiWidget, DisabledWidgetStillDrawn)
{
    // Disabling a widget must not suppress its draw commands.
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    auto* child = root.add_child<cd::ui::Panel>();
    child->set_bounds({ 0.0F, 0.0F, 50.0F, 50.0F });
    child->set_enabled(false);

    std::vector<cd::ui::DrawCommand> cmds;
    root.collect_draw_commands(cmds);
    // root rect + child rect — both emitted even though child is disabled.
    EXPECT_EQ(cmds.size(), 2u);
}

TEST(UiWidget, DisabledClickNotFired)
{
    // dispatch_click on a root that contains a disabled button must NOT
    // fire the button's callback.
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 200.0F, 200.0F });
    auto* btn = root.add_child<cd::ui::Button>("Disabled");
    btn->set_bounds({ 10.0F, 10.0F, 100.0F, 40.0F });
    btn->set_enabled(false);

    int fired { 0 };
    btn->set_on_click([&] { ++fired; });

    // Click lands in button bounds, but button is disabled → no callback.
    root.dispatch_click(50.0F, 30.0F);
    EXPECT_EQ(fired, 0);
    EXPECT_EQ(btn->click_count(), 0u);
}

TEST(UiWidget, EnabledAccessorRoundTrip)
{
    cd::ui::Panel p {};
    EXPECT_TRUE(p.enabled());
    p.set_enabled(false);
    EXPECT_FALSE(p.enabled());
    p.set_enabled(true);
    EXPECT_TRUE(p.enabled());
}

// ---- Widget: z-order among 3 overlapping siblings ------------------------

TEST(UiWidget, ZOrderTopmostAmongThreeSiblings)
{
    // Three overlapping siblings: bottom < mid < top (added in that order).
    // A click at the overlapping region must return the top-most (last-added).
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 200.0F, 200.0F });
    auto* bottom = root.add_child<cd::ui::Panel>();
    bottom->set_bounds({ 10.0F, 10.0F, 100.0F, 100.0F });
    auto* mid = root.add_child<cd::ui::Panel>();
    mid->set_bounds({ 20.0F, 20.0F, 80.0F, 80.0F });
    auto* top = root.add_child<cd::ui::Panel>();
    top->set_bounds({ 30.0F, 30.0F, 60.0F, 60.0F });

    // Point inside all three: topmost wins.
    EXPECT_EQ(root.hit_test(50.0F, 50.0F), top);
    // Point inside bottom+mid but outside top: mid wins.
    EXPECT_EQ(root.hit_test(25.0F, 25.0F), mid);
    // Point inside bottom only.
    EXPECT_EQ(root.hit_test(12.0F, 12.0F), bottom);
}

// ---- Widget: empty tree / edge cases -------------------------------------

TEST(UiWidget, EmptyRootHitTestReturnsSelf)
{
    cd::ui::Panel root {};
    root.set_bounds({ 0.0F, 0.0F, 50.0F, 50.0F });
    // No children; hit inside bounds returns root.
    EXPECT_EQ(root.hit_test(10.0F, 10.0F), &root);
    // Hit outside returns nullptr.
    EXPECT_EQ(root.hit_test(100.0F, 100.0F), nullptr);
}

TEST(UiWidget, EmptyRootDrawCommandsProducesOneRect)
{
    cd::ui::Panel root {};
    root.set_bounds({ 5.0F, 5.0F, 40.0F, 40.0F });
    std::vector<cd::ui::DrawCommand> cmds;
    root.collect_draw_commands(cmds);
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].kind, cd::ui::DrawKind::kRect);
}

// ---- Widget: id accessor --------------------------------------------------

TEST(UiWidget, IdRoundTrip)
{
    cd::ui::Panel p {};
    EXPECT_EQ(p.id(), 0u);
    p.set_id(42u);
    EXPECT_EQ(p.id(), 42u);
    p.set_id(0u);
    EXPECT_EQ(p.id(), 0u);
}

// ---- Button: empty-label emits only rect ---------------------------------

TEST(UiButton, EmptyLabelEmitsOnlyRect)
{
    // Button with no label: emit_draw_ must emit the background rect only
    // (the label_ guard `if (!label_.empty())` skips the text command).
    cd::ui::Button btn {};   // default-constructed, label_ is ""
    btn.set_bounds({ 0.0F, 0.0F, 40.0F, 20.0F });
    std::vector<cd::ui::DrawCommand> cmds;
    btn.collect_draw_commands(cmds);
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].kind, cd::ui::DrawKind::kRect);
}

TEST(UiButton, LabelAccessor)
{
    cd::ui::Button btn { "Hello" };
    EXPECT_EQ(btn.label(), "Hello");
    btn.set_label("World");
    EXPECT_EQ(btn.label(), "World");
}

// ---- Label: color accessor + draw color propagates -----------------------

TEST(UiLabel, ColorAccessorAndDraw)
{
    cd::ui::Label lbl { "hi", cd::ui::Color { 0.5F, 0.0F, 0.0F, 1.0F } };
    lbl.set_bounds({ 0.0F, 0.0F, 50.0F, 20.0F });
    EXPECT_FLOAT_EQ(lbl.color().r, 0.5F);
    std::vector<cd::ui::DrawCommand> cmds;
    lbl.collect_draw_commands(cmds);
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].kind, cd::ui::DrawKind::kText);
    EXPECT_FLOAT_EQ(cmds[0].color.r, 0.5F);
    EXPECT_EQ(cmds[0].text, "hi");
}

TEST(UiLabel, SetTextMutates)
{
    cd::ui::Label lbl { "before" };
    lbl.set_text("after");
    EXPECT_EQ(lbl.text(), "after");
}

// ---- Panel: background accessor + draw color propagates ------------------

TEST(UiPanel, BackgroundColorInDrawCommand)
{
    const cd::ui::Color red { 1.0F, 0.0F, 0.0F, 1.0F };
    cd::ui::Panel p { red };
    p.set_bounds({ 0.0F, 0.0F, 100.0F, 100.0F });
    std::vector<cd::ui::DrawCommand> cmds;
    p.collect_draw_commands(cmds);
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_FLOAT_EQ(cmds[0].color.r, 1.0F);
    EXPECT_FLOAT_EQ(cmds[0].color.g, 0.0F);
    p.set_background({ 0.0F, 1.0F, 0.0F, 1.0F });
    EXPECT_FLOAT_EQ(p.background().g, 1.0F);
}

// ---- Tooltip: reset and delay accessor -----------------------------------

TEST(Tooltip, ResetClearsState)
{
    cd::ui::Tooltip t;
    t.set_delay(0.1F);
    t.update(7, 0.0F);
    t.update(7, 0.2F);
    EXPECT_TRUE(t.visible());
    t.reset();
    EXPECT_FALSE(t.visible());
    EXPECT_EQ(t.target(), 0u);
}

TEST(Tooltip, DelayAccessorRoundTrip)
{
    cd::ui::Tooltip t;
    t.set_delay(1.5F);
    EXPECT_FLOAT_EQ(t.delay(), 1.5F);
}

TEST(Tooltip, NegativeDelayClampedToZero)
{
    cd::ui::Tooltip t;
    t.set_delay(-0.5F);
    EXPECT_FLOAT_EQ(t.delay(), 0.0F);
}

// ---- ProgressBar: total accessor + set_total clamps existing done --------

TEST(ProgressBar, TotalAccessor)
{
    cd::ui::ProgressBar p;
    p.set_total(42u);
    EXPECT_EQ(p.total(), 42u);
}

TEST(ProgressBar, SetTotalClampsExistingDone)
{
    cd::ui::ProgressBar p;
    p.set_total(10u);
    p.tick(8u);
    EXPECT_EQ(p.done(), 8u);
    // Shrink total below current done — done must clamp.
    p.set_total(5u);
    EXPECT_EQ(p.done(), 5u);
    EXPECT_TRUE(p.is_complete());
}

// ---- Spinner: speed_rps accessor + default angle zero --------------------

TEST(Spinner, DefaultAngleIsZero)
{
    const cd::ui::Spinner s;
    EXPECT_FLOAT_EQ(s.angle(), 0.0F);
    EXPECT_FLOAT_EQ(s.speed_rps(), 1.0F);
}

TEST(Spinner, SpeedRpsAccessor)
{
    cd::ui::Spinner s;
    s.set_speed_rps(2.5F);
    EXPECT_FLOAT_EQ(s.speed_rps(), 2.5F);
}

// ---- TabBar: clear, tabs(), active_index, empty active -------------------

TEST(TabBar, EmptyBarActiveReturnsNullptr)
{
    cd::ui::TabBar b;
    EXPECT_EQ(b.active(), nullptr);
    EXPECT_EQ(b.size(), 0u);
}

TEST(TabBar, ClearEmptiesBar)
{
    cd::ui::TabBar b;
    b.add(1, "a");
    b.add(2, "b");
    b.clear();
    EXPECT_EQ(b.size(), 0u);
    EXPECT_EQ(b.active(), nullptr);
}

TEST(TabBar, TabsSpanMatchesAdded)
{
    cd::ui::TabBar b;
    b.add(10, "ten");
    b.add(20, "twenty");
    const auto& tabs = b.tabs();
    ASSERT_EQ(tabs.size(), 2u);
    EXPECT_EQ(tabs[0].id, 10u);
    EXPECT_EQ(tabs[1].id, 20u);
    EXPECT_EQ(tabs[0].label, "ten");
}

TEST(TabBar, ActiveIndexMatchesSet)
{
    cd::ui::TabBar b;
    b.add(1, "a");
    b.add(2, "b");
    b.add(3, "c");
    b.set_active(2);
    EXPECT_EQ(b.active_index(), 1u);
    b.set_active(3);
    EXPECT_EQ(b.active_index(), 2u);
}

// ---- ToastAnim: slide-in, steady, fade-out phases ------------------------

#include <cd/ui/ToastAnim.hpp>

TEST(ToastAnim, AtAgeZeroAlphaIsZeroAndOffset)
{
    const auto r = cd::ui::toast_anim(0.0, cd::ui::ToastDirection::kFromRight);
    EXPECT_FLOAT_EQ(r.alpha, 0.0F);
    // Offset should be max slide at start.
    EXPECT_GT(r.x_offset, 0.0F);
    EXPECT_FLOAT_EQ(r.y_offset, 0.0F);
}

TEST(ToastAnim, SteadyPhaseAlphaIsOne)
{
    // At 500 ms (well past 150 ms slide-in, before 1850 ms fade), alpha == 1.
    const auto r = cd::ui::toast_anim(500.0);
    EXPECT_FLOAT_EQ(r.alpha, 1.0F);
    EXPECT_FLOAT_EQ(r.x_offset, 0.0F);
    EXPECT_FLOAT_EQ(r.y_offset, 0.0F);
}

TEST(ToastAnim, FadeOutPhaseAlphaDecreases)
{
    // At 1925 ms (halfway through 150 ms fade-out starting at 1850 ms).
    const auto r = cd::ui::toast_anim(1925.0);
    EXPECT_GT(r.alpha, 0.0F);
    EXPECT_LT(r.alpha, 1.0F);
}

TEST(ToastAnim, ExpiredToastReturnsZeroAlpha)
{
    // Past the full 2000 ms lifetime, alpha clamps to 0.
    const auto r = cd::ui::toast_anim(2001.0);
    EXPECT_FLOAT_EQ(r.alpha, 0.0F);
}

TEST(ToastAnim, FromLeftXOffsetIsNegative)
{
    const auto r = cd::ui::toast_anim(0.0, cd::ui::ToastDirection::kFromLeft);
    EXPECT_LT(r.x_offset, 0.0F);
}

TEST(ToastAnim, FromTopYOffsetIsNegative)
{
    const auto r = cd::ui::toast_anim(0.0, cd::ui::ToastDirection::kFromTop);
    EXPECT_LT(r.y_offset, 0.0F);
    EXPECT_FLOAT_EQ(r.x_offset, 0.0F);
}

TEST(ToastAnim, FromBottomYOffsetIsPositive)
{
    const auto r = cd::ui::toast_anim(0.0, cd::ui::ToastDirection::kFromBottom);
    EXPECT_GT(r.y_offset, 0.0F);
    EXPECT_FLOAT_EQ(r.x_offset, 0.0F);
}

TEST(ToastAnim, OverloadWithCfgUsesFromRight)
{
    // The two-arg overload (age_ms, cfg) defaults to kFromRight.
    const cd::ui::ToastAnimCfg cfg {};
    const auto r = cd::ui::toast_anim(0.0, cfg);
    EXPECT_GT(r.x_offset, 0.0F);
}

TEST(ToastStackYOffset, IndexZeroIsZero)
{
    EXPECT_FLOAT_EQ(cd::ui::toast_stack_y_offset(0, 32.0F), 0.0F);
}

TEST(ToastStackYOffset, IndexOneIsHeightPlusGap)
{
    EXPECT_FLOAT_EQ(cd::ui::toast_stack_y_offset(1, 32.0F, 4.0F), 36.0F);
}

// ---- Anchor: center() integer division rounding + negative offsets -------

TEST(Anchor, CenterOddSizeRoundsDown)
{
    // center(101, 101): -101/2 == -50 (truncation toward zero).
    cd::ui::IntRect parent { 0, 0, 400, 400 };
    auto r = cd::ui::resolve(parent, cd::ui::center(101, 101));
    EXPECT_EQ(r.w, 100);  // symmetric -50/+50 truncation rounds the odd size DOWN to 100
    EXPECT_EQ(r.h, 100);
    // x = 400/2 - 50 = 150 (truncation; offset_max = +50, offset_min = -50)
    EXPECT_EQ(r.x, 150);
    EXPECT_EQ(r.y, 150);
}

TEST(Anchor, StretchWithNonZeroParentOrigin)
{
    // Stretch inside a parent that doesn't start at (0,0).
    cd::ui::IntRect parent { 50, 75, 300, 200 };
    auto r = cd::ui::resolve(parent, cd::ui::stretch());
    EXPECT_EQ(r.x, 50);
    EXPECT_EQ(r.y, 75);
    EXPECT_EQ(r.w, 300);
    EXPECT_EQ(r.h, 200);
}

// ---- Glyph-layout SEAL: text is DrawKind::kText only ---------------------
// Text rendering in cd::ui is intentionally a DrawKind::kText command
// (position + string_view). Glyph layout, font metrics, and atlas UV
// computation belong to cd::ui_font. This test seals that contract: a
// Label's draw command carries the raw string, not measured glyph quads.

TEST(UiLabel, TextDrawCommandCarriesRawString)
{
    cd::ui::Label lbl { "seal-glyph-layout" };
    lbl.set_bounds({ 0.0F, 0.0F, 200.0F, 20.0F });
    std::vector<cd::ui::DrawCommand> cmds;
    lbl.collect_draw_commands(cmds);
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].kind, cd::ui::DrawKind::kText);
    // The text field is the raw string — no glyph decomposition.
    EXPECT_EQ(cmds[0].text, "seal-glyph-layout");
    // Rect carries the widget's bounds verbatim (no layout pass applied).
    EXPECT_FLOAT_EQ(cmds[0].rect.w, 200.0F);
}
