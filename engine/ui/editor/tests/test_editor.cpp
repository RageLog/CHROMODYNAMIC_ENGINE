// =============================================================================
// CHROMODYNAMIC — cd::editor tests
//
// End-to-end test of the integration shell: drive input via push_event,
// run tick(), inspect the resulting draw commands + selection state.
// =============================================================================
#include <algorithm>
#include <cd/editor/CommandPalette.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/Editor.hpp>
#include <cd/editor/SelectionSet.hpp>
#include <cd/editor/TransformCommands.hpp>
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

// ---- Phase 12.C / v0.28.0 — EditHistory + TransformCommands ----

namespace
{

class CounterCommand final : public cd::editor::ICommand
{
public:
    explicit CounterCommand(int* sink) : sink_(sink) {}
    void apply() override { ++(*sink_); }
    void revert() override { --(*sink_); }
    [[nodiscard]] std::string_view label() const noexcept override { return "Counter"; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }
private:
    int* sink_;
};

TEST(EditHistory, PushAppliesImmediately)
{
    cd::editor::EditHistory hist;
    int counter = 0;
    hist.push(std::make_unique<CounterCommand>(&counter));
    EXPECT_EQ(counter, 1);
    EXPECT_TRUE(hist.can_undo());
    EXPECT_FALSE(hist.can_redo());
    EXPECT_EQ(hist.next_undo_label(), "Counter");
}

TEST(EditHistory, UndoRedoRoundTrip)
{
    cd::editor::EditHistory hist;
    int counter = 0;
    hist.push(std::make_unique<CounterCommand>(&counter));
    hist.push(std::make_unique<CounterCommand>(&counter));
    hist.push(std::make_unique<CounterCommand>(&counter));
    EXPECT_EQ(counter, 3);
    EXPECT_EQ(hist.undo_depth(), 3U);

    EXPECT_TRUE(hist.undo());
    EXPECT_EQ(counter, 2);
    EXPECT_TRUE(hist.undo());
    EXPECT_EQ(counter, 1);
    EXPECT_EQ(hist.redo_depth(), 2U);

    EXPECT_TRUE(hist.redo());
    EXPECT_EQ(counter, 2);
    EXPECT_TRUE(hist.redo());
    EXPECT_EQ(counter, 3);
    EXPECT_FALSE(hist.can_redo());
}

TEST(EditHistory, PushAfterUndoDiscardsRedoTrail)
{
    cd::editor::EditHistory hist;
    int counter = 0;
    hist.push(std::make_unique<CounterCommand>(&counter));
    hist.push(std::make_unique<CounterCommand>(&counter));
    EXPECT_TRUE(hist.undo());
    EXPECT_TRUE(hist.can_redo());

    hist.push(std::make_unique<CounterCommand>(&counter));
    EXPECT_FALSE(hist.can_redo());
    EXPECT_EQ(counter, 2);
}

TEST(EditHistory, EntryCapEvictsOldest)
{
    cd::editor::EditHistory::Config cfg;
    cfg.max_entries = 3;
    cd::editor::EditHistory hist { cfg };
    int counter = 0;
    for (int i = 0; i < 10; ++i)
        hist.push(std::make_unique<CounterCommand>(&counter));
    EXPECT_EQ(counter, 10);
    EXPECT_EQ(hist.undo_depth(), 3U);
    EXPECT_TRUE(hist.undo());
    EXPECT_TRUE(hist.undo());
    EXPECT_TRUE(hist.undo());
    EXPECT_EQ(counter, 7);
    EXPECT_FALSE(hist.can_undo());
}

TEST(EditHistory, ClearWipesBothStacks)
{
    cd::editor::EditHistory hist;
    int counter = 0;
    hist.push(std::make_unique<CounterCommand>(&counter));
    hist.push(std::make_unique<CounterCommand>(&counter));
    EXPECT_TRUE(hist.undo());
    hist.clear();
    EXPECT_FALSE(hist.can_undo());
    EXPECT_FALSE(hist.can_redo());
    EXPECT_EQ(hist.bytes_in_use(), 0U);
}

TEST(TransformCommands, TranslateUndoRedoRoundTrip)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    auto entity = scene.create_node();
    scene.local(entity)->value.position = cd::math::Vec3f { 1.0F, 2.0F, 3.0F };

    cd::editor::EditHistory hist;
    hist.push(std::make_unique<cd::editor::TranslateCommand>(
        scene, entity, cd::math::Vec3f { 5.0F, -1.0F, 0.5F }));
    EXPECT_FLOAT_EQ(scene.local(entity)->value.position.x, 6.0F);
    EXPECT_FLOAT_EQ(scene.local(entity)->value.position.y, 1.0F);
    EXPECT_FLOAT_EQ(scene.local(entity)->value.position.z, 3.5F);

    EXPECT_TRUE(hist.undo());
    EXPECT_FLOAT_EQ(scene.local(entity)->value.position.x, 1.0F);
    EXPECT_FLOAT_EQ(scene.local(entity)->value.position.y, 2.0F);
    EXPECT_FLOAT_EQ(scene.local(entity)->value.position.z, 3.0F);

    EXPECT_TRUE(hist.redo());
    EXPECT_FLOAT_EQ(scene.local(entity)->value.position.x, 6.0F);
    EXPECT_FLOAT_EQ(scene.local(entity)->value.position.y, 1.0F);
    EXPECT_FLOAT_EQ(scene.local(entity)->value.position.z, 3.5F);
}

TEST(TransformCommands, ScaleUndoRedoRoundTrip)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    auto entity = scene.create_node();
    scene.local(entity)->value.scale = cd::math::Vec3f { 2.0F, 2.0F, 2.0F };

    cd::editor::EditHistory hist;
    hist.push(std::make_unique<cd::editor::ScaleCommand>(
        scene, entity, cd::math::Vec3f { 1.5F, 1.5F, 1.5F }));
    EXPECT_FLOAT_EQ(scene.local(entity)->value.scale.x, 3.0F);

    EXPECT_TRUE(hist.undo());
    EXPECT_FLOAT_EQ(scene.local(entity)->value.scale.x, 2.0F);

    EXPECT_TRUE(hist.redo());
    EXPECT_FLOAT_EQ(scene.local(entity)->value.scale.x, 3.0F);
}

TEST(TransformCommands, RotateUndoRedoRoundTrip)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    auto entity = scene.create_node();
    const cd::math::Quatf original { 0.0F, 0.0F, 0.0F, 1.0F };
    const cd::math::Quatf target { 0.0F, 0.707F, 0.0F, 0.707F };
    scene.local(entity)->value.rotation = original;

    cd::editor::EditHistory hist;
    hist.push(std::make_unique<cd::editor::RotateCommand>(scene, entity, target));
    EXPECT_FLOAT_EQ(scene.local(entity)->value.rotation.y, target.y);

    EXPECT_TRUE(hist.undo());
    EXPECT_FLOAT_EQ(scene.local(entity)->value.rotation.y, original.y);
    EXPECT_FLOAT_EQ(scene.local(entity)->value.rotation.w, original.w);

    EXPECT_TRUE(hist.redo());
    EXPECT_FLOAT_EQ(scene.local(entity)->value.rotation.y, target.y);
}

TEST(CommandPalette, EmptyQueryReturnsAll)
{
    cd::editor::CommandPalette p;
    p.register_command(1, "Open File", [] {});
    p.register_command(2, "Save File", [] {});
    EXPECT_EQ(p.filter("").size(), 2u);
}

TEST(CommandPalette, FuzzyFindsSubsequence)
{
    cd::editor::CommandPalette p;
    p.register_command(1, "Open File", [] {});
    p.register_command(2, "Open Folder", [] {});
    p.register_command(3, "Save All", [] {});
    const auto hits = p.filter("of");
    EXPECT_GE(hits.size(), 2u);
    EXPECT_NE(std::find(hits.begin(), hits.end(), std::size_t { 0 }), hits.end());
    EXPECT_NE(std::find(hits.begin(), hits.end(), std::size_t { 1 }), hits.end());
}

TEST(CommandPalette, NoMatchReturnsEmpty)
{
    cd::editor::CommandPalette p;
    p.register_command(1, "Open File", [] {});
    EXPECT_TRUE(p.filter("xyz").empty());
}

TEST(CommandPalette, InvokeFiresAction)
{
    cd::editor::CommandPalette p;
    int fired = 0;
    p.register_command(7, "Hit Me", [&] { ++fired; });
    EXPECT_TRUE(p.invoke(0));
    EXPECT_EQ(fired, 1);
}

TEST(CommandPalette, ReRegisterOverwrites)
{
    cd::editor::CommandPalette p;
    p.register_command(1, "Old", [] {});
    p.register_command(1, "New", [] {});
    EXPECT_EQ(p.size(), 1u);
    EXPECT_EQ(p.at(0).label, "New");
}

}  // namespace

TEST(SelectionSet, EmptyByDefault)
{
    cd::editor::SelectionSet s;
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s.size(), 0u);
    EXPECT_FALSE(s.primary().is_valid());
}

TEST(SelectionSet, AddInsertsAndSetsPrimary)
{
    cd::editor::SelectionSet s;
    cd::ecs::Entity a { 1, 1 };
    cd::ecs::Entity b { 2, 1 };
    s.add(a);
    s.add(b);
    EXPECT_TRUE(s.contains(a));
    EXPECT_TRUE(s.contains(b));
    EXPECT_EQ(s.primary(), b);   // most-recently-added
    EXPECT_EQ(s.size(), 2u);
}

TEST(SelectionSet, DuplicateAddIsNoOp)
{
    cd::editor::SelectionSet s;
    cd::ecs::Entity a { 5, 1 };
    s.add(a);
    s.add(a);
    EXPECT_EQ(s.size(), 1u);
}

TEST(SelectionSet, RemoveDropsAndAdjustsPrimary)
{
    cd::editor::SelectionSet s;
    cd::ecs::Entity a { 1, 1 };
    cd::ecs::Entity b { 2, 1 };
    s.add(a); s.add(b);
    s.remove(b);
    EXPECT_FALSE(s.contains(b));
    EXPECT_TRUE(s.primary().is_valid());
    EXPECT_NE(s.primary(), b);
}

TEST(SelectionSet, ClearResetsEverything)
{
    cd::editor::SelectionSet s;
    s.add(cd::ecs::Entity { 1, 1 });
    s.add(cd::ecs::Entity { 2, 1 });
    s.clear();
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(s.primary().is_valid());
}
