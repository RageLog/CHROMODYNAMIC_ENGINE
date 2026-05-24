// =============================================================================
// CHROMODYNAMIC — cd::ui tests
// =============================================================================
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

#include <cd/ui/Anchor.hpp>

TEST(Anchor, StretchFillsParent)
{
    cd::ui::Rect parent { 0, 0, 1920, 1080 };
    auto r = cd::ui::resolve(parent, cd::ui::stretch());
    EXPECT_EQ(r.x, 0);
    EXPECT_EQ(r.y, 0);
    EXPECT_EQ(r.w, 1920);
    EXPECT_EQ(r.h, 1080);
}

TEST(Anchor, CenterPlacesFixedRect)
{
    cd::ui::Rect parent { 0, 0, 800, 600 };
    auto r = cd::ui::resolve(parent, cd::ui::center(200, 100));
    EXPECT_EQ(r.w, 200);
    EXPECT_EQ(r.h, 100);
    EXPECT_EQ(r.x, 300);  // (800 - 200) / 2
    EXPECT_EQ(r.y, 250);  // (600 - 100) / 2
}

TEST(Anchor, OffsetMarginShrinksFromEdges)
{
    cd::ui::Rect parent { 0, 0, 1000, 1000 };
    cd::ui::Anchor a { 0.0F, 0.0F, 1.0F, 1.0F, 50, 50, -50, -50 };
    auto r = cd::ui::resolve(parent, a);
    EXPECT_EQ(r.x, 50);
    EXPECT_EQ(r.y, 50);
    EXPECT_EQ(r.w, 900);
    EXPECT_EQ(r.h, 900);
}

TEST(Anchor, NestedParentOriginRespected)
{
    cd::ui::Rect parent { 100, 200, 400, 300 };
    auto r = cd::ui::resolve(parent, cd::ui::stretch());
    EXPECT_EQ(r.x, 100);
    EXPECT_EQ(r.y, 200);
    EXPECT_EQ(r.w, 400);
    EXPECT_EQ(r.h, 300);
}

#include <cd/ui/Theme.hpp>

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

#include <cd/ui/Tooltip.hpp>

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
