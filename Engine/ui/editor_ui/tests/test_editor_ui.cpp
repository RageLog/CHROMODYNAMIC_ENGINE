// =============================================================================
// CHROMODYNAMIC — cd::editor_ui tests
// =============================================================================
#include <cd/ecs/World.hpp>
#include <cd/editor_ui/EditorWidgets.hpp>
#include <cd/scene/Scene.hpp>
#include <gtest/gtest.h>

#include <cmath>

namespace
{

TEST(EditorUI, InspectorShowsNoTargetByDefault)
{
    cd::editor_ui::PropertyInspector pi;
    EXPECT_EQ(pi.header_text(), "Inspector");
    EXPECT_EQ(pi.body_text(), "(no target)");
}

TEST(EditorUI, InspectorRefreshDisplaysTransform)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    s.local(e)->value.position = { 1.0F, 2.0F, 3.0F };

    cd::editor_ui::PropertyInspector pi;
    pi.set_target(&s, e);
    EXPECT_NE(pi.header_text().find("entity"), std::string::npos);
    EXPECT_NE(pi.body_text().find("1.00"), std::string::npos);
    EXPECT_NE(pi.body_text().find("2.00"), std::string::npos);
    EXPECT_NE(pi.body_text().find("3.00"), std::string::npos);
}

TEST(EditorUI, InspectorHandlesDeadTarget)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    cd::editor_ui::PropertyInspector pi;
    pi.set_target(&s, e);
    s.destroy_node(e);
    pi.refresh();
    EXPECT_EQ(pi.body_text(), "(no target)");
}

TEST(EditorUI, GizmoClickAdvancesAxis)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    cd::editor_ui::TransformGizmo g { 1.5F };
    g.set_target(&s, e);
    g.click_axis(0);  // X
    g.click_axis(0);
    g.click_axis(1);  // Y
    const auto& p = s.local(e)->value.position;
    EXPECT_FLOAT_EQ(p.x, 3.0F);
    EXPECT_FLOAT_EQ(p.y, 1.5F);
    EXPECT_FLOAT_EQ(p.z, 0.0F);
    EXPECT_EQ(g.total_clicks(), 3U);
}

TEST(EditorUI, GizmoIgnoresClickWithoutScene)
{
    cd::editor_ui::TransformGizmo g { 1.0F };
    // No target / no scene — must be a no-op, NOT a crash.
    g.click_axis(0);
    g.click_axis(2);
    EXPECT_EQ(g.total_clicks(), 2U);  // buttons still register the click
}

TEST(EditorUI, SceneTreeViewRowCountMatchesGraph)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto root = s.create_node();
    auto a = s.create_node();
    auto b = s.create_node();
    auto c = s.create_node();
    ASSERT_TRUE(s.attach(a, root));
    ASSERT_TRUE(s.attach(b, root));
    ASSERT_TRUE(s.attach(c, a));

    cd::editor_ui::SceneTreeView v;
    v.set_bounds({ 0.0F, 0.0F, 200.0F, 200.0F });
    v.set_scene(&s, root);
    // root + a + c + b == 4 entries (a's child c precedes b because the
    // depth-first walk descends before the next sibling).
    EXPECT_EQ(v.row_count(), 4U);
}

TEST(EditorUI, SceneTreeViewEmptyWhenRootDead)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    cd::editor_ui::SceneTreeView v;
    v.set_scene(&s, cd::ecs::Entity {});  // never created
    EXPECT_EQ(v.row_count(), 0U);
}

}  // namespace
