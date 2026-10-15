// =============================================================================
// CHROMODYNAMIC — cd::ui::widgets EditorAndHudWidgets tests (Wave 2.4)
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/EditorAndHudWidgets.hpp>

#include <gtest/gtest.h>

namespace w = cd::ui::widgets;
namespace r = cd::ui::renderer;

namespace
{

[[nodiscard]] w::InputState make_ptr(float mx, float my,
                                     bool down, bool pressed, bool released)
{
    w::InputState in {};
    in.pointer.mouse_x       = mx;
    in.pointer.mouse_y       = my;
    in.pointer.left_down     = down;
    in.pointer.left_pressed  = pressed;
    in.pointer.left_released = released;
    return in;
}

}  // namespace

TEST(EditorAndHudWidgets, NumericDragBoxScrubsAndResets)
{
    w::NumericDragBox box(10.0F, 0.0F, 100.0F, 0.5F);
    box.set_rect({ 0.0F, 0.0F, 80.0F, 24.0F });

    EXPECT_FALSE(box.tick(make_ptr(20.0F, 12.0F, true, true, false)));
    EXPECT_TRUE(box.is_dragging());
    EXPECT_TRUE(box.tick(make_ptr(40.0F, 12.0F, true, false, false)));
    EXPECT_FLOAT_EQ(box.value(), 20.0F);  // 10 + (40-20)*0.5

    box.reset_to_default();
    EXPECT_FLOAT_EQ(box.value(), 10.0F);
}

TEST(EditorAndHudWidgets, Vec3ControlBadgeClickResetsAxisAndDraws)
{
    w::Vec3Control vec3(5.0F, 12.0F, -3.0F, 0.0F);
    vec3.set_rect({ 0.0F, 0.0F, 240.0F, 24.0F });

    const auto bx = vec3.badge_rect(0U);
    ASSERT_TRUE(bx.is_valid());

    // Click X badge -> resets X from 5.0 to 0.0
    (void)vec3.tick(make_ptr(bx.x + 2.0F, bx.y + 2.0F, true, true, false));
    EXPECT_TRUE(vec3.tick(make_ptr(bx.x + 2.0F, bx.y + 2.0F, false, false, true)));
    EXPECT_FLOAT_EQ(vec3.x(), 0.0F);
    EXPECT_FLOAT_EQ(vec3.y(), 12.0F);
    EXPECT_FLOAT_EQ(vec3.z(), -3.0F);

    r::DrawBatcher batcher;
    batcher.begin_frame();
    vec3.draw(batcher, nullptr, w::Theme {});
    EXPECT_GT(batcher.vertex_count(), 0U);
}

TEST(EditorAndHudWidgets, SplitterAndScrollAreaInteractAndDraw)
{
    w::Splitter sp(/*vertical_split=*/true, 0.5F);
    sp.set_rect({ 0.0F, 0.0F, 206.0F, 100.0F });
    sp.set_bar_thickness(6.0F);

    const auto bar = sp.bar_rect();
    EXPECT_FLOAT_EQ(bar.x, 100.0F);

    (void)sp.tick(make_ptr(bar.x + 1.0F, 50.0F, true, true, false));
    EXPECT_TRUE(sp.tick(make_ptr(153.0F, 50.0F, true, false, false)));
    EXPECT_GT(sp.ratio(), 0.70F);

    w::ScrollArea sa;
    sa.set_rect({ 0.0F, 0.0F, 200.0F, 100.0F });
    sa.set_content_height(400.0F);
    sa.scroll_by(150.0F);
    EXPECT_FLOAT_EQ(sa.scroll_y(), 150.0F);

    r::DrawBatcher batcher;
    batcher.begin_frame();
    sp.draw(batcher, w::Theme {});
    sa.begin_draw(batcher, w::Theme {});
    sa.end_draw(batcher, w::Theme {});
    EXPECT_GT(batcher.vertex_count(), 0U);
}

TEST(EditorAndHudWidgets, MenuBarContextMenuToastTooltipNodeGraphFlameAndHud)
{
    w::MenuBar mb;
    mb.set_rect({ 0.0F, 0.0F, 400.0F, 24.0F });
    int file_new_clicks = 0;
    mb.add_menu("File", { w::MenuAction { "New Stage", [&] { ++file_new_clicks; }, true } });

    // Open File menu
    EXPECT_TRUE(mb.tick(make_ptr(10.0F, 10.0F, true, true, false)));
    EXPECT_EQ(mb.active_menu(), 0U);
    // Click first item at y = 24 + 10 = 34
    (void)mb.tick(make_ptr(20.0F, 34.0F, false, false, true));
    EXPECT_EQ(file_new_clicks, 1);

    w::ToastQueue toasts;
    toasts.push("Stage Saved", w::ToastSeverity::kSuccess, 1.0F);
    EXPECT_EQ(toasts.active_count(), 1U);
    toasts.tick(0.5F);
    EXPECT_EQ(toasts.active_count(), 1U);

    w::Tooltip tip;
    tip.set_delay(0.2F);
    tip.update(true, 50.0F, 50.0F, "Spawn Vessel", 0.25F);
    EXPECT_TRUE(tip.is_visible());

    w::NodeGraphCanvas graph;
    graph.set_rect({ 0.0F, 0.0F, 400.0F, 300.0F });
    const auto n1 = graph.add_node(w::GraphNode { 1U, "PBR Surface", 20.0F, 20.0F, 120.0F, 80.0F });
    const auto n2 = graph.add_node(w::GraphNode { 2U, "Output", 200.0F, 20.0F, 120.0F, 80.0F });
    EXPECT_TRUE(graph.add_wire(w::GraphWire { n1, 0U, n2, 0U }));

    // Drag node 1
    EXPECT_TRUE(graph.tick(make_ptr(40.0F, 40.0F, true, true, false)));
    EXPECT_EQ(graph.selected_node(), n1);
    EXPECT_TRUE(graph.tick(make_ptr(60.0F, 50.0F, true, false, false)));
    EXPECT_FLOAT_EQ(graph.nodes()[0].x, 40.0F);

    w::FlameChart flame;
    flame.set_rect({ 0.0F, 0.0F, 300.0F, 60.0F });
    flame.set_bars({ w::FlameScopeBar { "GBuffer", 0.0F, 2.5F, 0U } });

    w::Sparkline spark(10U);
    spark.set_rect({ 0.0F, 0.0F, 100.0F, 30.0F });
    spark.push_sample(16.6F);
    spark.push_sample(14.2F);
    EXPECT_GT(spark.average(), 15.0F);

    w::GaugeBar hp(0.75F);
    hp.set_rect({ 10.0F, 10.0F, 200.0F, 16.0F });

    w::Crosshair reticle;
    reticle.set_center(640.0F, 360.0F);
    reticle.set_spread(6.0F, 10.0F, 2.0F);

    w::Minimap radar;
    radar.set_rect({ 10.0F, 10.0F, 120.0F, 120.0F });
    radar.set_player_pose(0.0F, 0.0F, 50.0F);
    radar.set_blips({ w::MinimapBlip { 10.0F, -15.0F } });

    w::InventoryGrid inv(4U, 2U);
    inv.set_rect({ 0.0F, 0.0F, 160.0F, 80.0F });
    inv.set_slot_item(1U, 42U);
    const auto s1 = inv.slot_rect(1U);
    EXPECT_TRUE(inv.tick(make_ptr(s1.x + 4.0F, s1.y + 4.0F, true, true, false)));
    EXPECT_EQ(inv.selected_slot(), 1U);

    r::DrawBatcher batcher;
    batcher.begin_frame();
    w::Theme theme {};
    mb.draw(batcher, nullptr, theme);
    toasts.draw(batcher, nullptr, theme, { 0.0F, 0.0F, 1280.0F, 720.0F });
    tip.draw(batcher, nullptr, theme);
    graph.draw(batcher, nullptr, theme);
    flame.draw(batcher, nullptr, theme);
    spark.draw(batcher, theme);
    hp.draw(batcher, theme);
    reticle.draw(batcher);
    radar.draw(batcher, theme);
    inv.draw(batcher, theme);
    EXPECT_GT(batcher.vertex_count(), 100U);
}
