// =============================================================================
// CHROMODYNAMIC — cd::editor_ui tests
// =============================================================================
#include <cd/ecs/World.hpp>
#include <cd/editor/ui/EditorWidgets.hpp>
#include <cd/scene/Scene.hpp>
#include <gtest/gtest.h>

#include <cmath>

namespace
{

TEST(EditorUI, InspectorShowsNoTargetByDefault)
{
    cd::editor::ui::PropertyInspector pi;
    EXPECT_EQ(pi.header_text(), "Inspector");
    EXPECT_EQ(pi.body_text(), "(no target)");
}

TEST(EditorUI, InspectorRefreshDisplaysTransform)
{
    cd::ecs::World w;
    cd::scene::Scene s { w };
    auto e = s.create_node();
    s.local(e)->value.position = { 1.0F, 2.0F, 3.0F };

    cd::editor::ui::PropertyInspector pi;
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
    cd::editor::ui::PropertyInspector pi;
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
    cd::editor::ui::TransformGizmo g { 1.5F };
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
    cd::editor::ui::TransformGizmo g { 1.0F };
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

    cd::editor::ui::SceneTreeView v;
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
    cd::editor::ui::SceneTreeView v;
    v.set_scene(&s, cd::ecs::Entity {});  // never created
    EXPECT_EQ(v.row_count(), 0U);
}

}  // namespace

// =============================================================================
// PaletteRegistry + EscChain tests (new, from docs/EDITOR_LESSONS_LEARNED.md).
// =============================================================================
#include <cd/editor/ui/AssetPalette.hpp>
#include <cd/editor/ui/EscChain.hpp>

namespace {

TEST(EditorUI, PaletteRegistryStartsEmpty)
{
    cd::editor::ui::PaletteRegistry r;
    EXPECT_EQ(r.size(), 0U);
}

TEST(EditorUI, PaletteRegistrySeedsDefaultEntries)
{
    cd::editor::ui::PaletteRegistry r;
    r.seed_defaults();
    EXPECT_GE(r.size(), 10U);
    EXPECT_NE(r.find("mesh.cube"), nullptr);
    EXPECT_NE(r.find("light.spot"), nullptr);
    EXPECT_NE(r.find("audio.source"), nullptr);
}

TEST(EditorUI, PaletteRegistryFilterByCategory)
{
    cd::editor::ui::PaletteRegistry r;
    r.seed_defaults();
    const auto lights = r.filter(cd::editor::ui::PaletteCategory::kLight);
    EXPECT_EQ(lights.size(), 4U);  // dir + point + spot + rect
}

TEST(EditorUI, EscChainEmptyReturnsFalse)
{
    cd::editor::ui::EscChain c;
    EXPECT_FALSE(c.handle());
}

TEST(EditorUI, EscChainHigherPriorityRunsFirst)
{
    cd::editor::ui::EscChain c;
    int last_run = -1;
    c.register_handler(cd::editor::ui::EscPriority::kSelection,
        [&]{ last_run = 3; return false; });
    c.register_handler(cd::editor::ui::EscPriority::kActiveDrag,
        [&]{ last_run = 0; return true; });   // consumes
    c.register_handler(cd::editor::ui::EscPriority::kPalette,
        [&]{ last_run = 2; return false; });
    EXPECT_TRUE(c.handle());
    EXPECT_EQ(last_run, 0);  // ActiveDrag ran + consumed; chain stopped
}

TEST(EditorUI, EscChainFallsThroughWhenAllHandlersDecline)
{
    cd::editor::ui::EscChain c;
    bool first = false, second = false;
    c.register_handler(cd::editor::ui::EscPriority::kModalDialog,
        [&]{ first = true; return false; });
    c.register_handler(cd::editor::ui::EscPriority::kPalette,
        [&]{ second = true; return false; });
    EXPECT_FALSE(c.handle());
    EXPECT_TRUE(first);
    EXPECT_TRUE(second);
}

}  // namespace
