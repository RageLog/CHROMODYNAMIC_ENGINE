// =============================================================================
// CHROMODYNAMIC — cd::editor tests
//
// End-to-end test of the integration shell: drive input via push_event,
// run tick(), inspect the resulting draw commands + selection state.
// =============================================================================
#include <cd/editor/Editor.hpp>
#include <gtest/gtest.h>

namespace
{

cd::input::InputEvent left_click_at(float x, float y)
{
    cd::input::InputEvent e {};
    e.kind = cd::input::EventKind::kMouseButtonDown;
    e.mouse_button = cd::input::MouseButton::kLeft;
    e.mouse_x = x;
    e.mouse_y = y;
    return e;
}

TEST(Editor, ConstructionBuildsThreePaneLayout)
{
    cd::editor::Editor ed {};
    // Root + 3 panes (tree, inspector, gizmo). The widget tree's child
    // count exposes this directly.
    EXPECT_EQ(ed.root().child_count(), 3U);
    EXPECT_TRUE(ed.world().is_alive(ed.scene_root()));
}

TEST(Editor, TickProducesDrawCommandsForEveryVisiblePane)
{
    cd::editor::Editor ed {};
    auto cmds = ed.tick();
    // Root panel + tree-panel + inspector-panel + 2 inspector labels
    // + gizmo-container (transparent: still a rect) + 3 axis buttons (rect
    // + text each) = 1 + 1 + 1 + 2 + 1 + 6 = 12 commands at minimum. Just
    // verify it's non-trivial and starts with the root rect.
    ASSERT_FALSE(cmds.empty());
    EXPECT_EQ(cmds.front().kind, cd::ui::DrawKind::kRect);
    EXPECT_GE(cmds.size(), 10U);
}

TEST(Editor, SelectAimsInspectorAndGizmo)
{
    cd::editor::Editor ed {};
    auto e = ed.scene().create_node();
    ed.select(e);
    EXPECT_EQ(ed.selection(), e);
    EXPECT_EQ(ed.inspector().target(), e);
    EXPECT_EQ(ed.gizmo().target(), e);
}

TEST(Editor, ClickRoutedThroughInputContext)
{
    cd::editor::Editor ed {};
    auto e = ed.scene().create_node();
    ed.select(e);

    // Find a gizmo axis button's screen-space center and click it via
    // InputContext — exercises the full chain: InputContext.push_event →
    // dispatch_input_ → root_.dispatch_click → gizmo button → apply_delta.
    const auto& gz_bounds = ed.gizmo().bounds();
    // The X axis button is at offset (0, 0) inside the gizmo container.
    const float ax = gz_bounds.x + 12.0F;
    const float ay = gz_bounds.y + 12.0F;
    ed.input().push_event(left_click_at(ax, ay));
    ed.tick();
    EXPECT_EQ(ed.gizmo().total_clicks(), 1U);
}

TEST(Editor, InputContextStateUpdatedByPushEvent)
{
    cd::editor::Editor ed {};
    cd::input::InputEvent e {};
    e.kind = cd::input::EventKind::kKeyDown;
    e.key = cd::input::KeyCode::kW;
    ed.input().push_event(e);
    EXPECT_TRUE(ed.input().state().is_key_down(cd::input::KeyCode::kW));
}

}  // namespace
