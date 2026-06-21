// =============================================================================
// CHROMODYNAMIC — cd::editor tests
//
// End-to-end test of the integration shell: drive input via push_event,
// run tick(), inspect the resulting draw commands + selection state.
// =============================================================================
#include <algorithm>
#include <cd/editor/Bookmark.hpp>
#include <cd/editor/CommandPalette.hpp>
#include <cd/editor/EditHistory.hpp>
#include <cd/editor/Editor.hpp>
#include <cd/editor/HierarchyView.hpp>
#include <cd/editor/MenuBar.hpp>
#include <cd/editor/PreferencesStore.hpp>
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
    EXPECT_NE(std::ranges::find(hits, std::size_t { 0 }), hits.end());
    EXPECT_NE(std::ranges::find(hits, std::size_t { 1 }), hits.end());
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

TEST(BookmarkSet, AddAndApply)
{
    cd::editor::BookmarkSet bs;
    cd::camera::Camera cam;
    cam.eye = { 5, 6, 7 };
    cam.target = { 8, 9, 10 };
    bs.add("front", cam);
    EXPECT_EQ(bs.size(), 1u);

    cd::camera::Camera other;
    EXPECT_TRUE(bs.apply(0, other));
    EXPECT_FLOAT_EQ(other.eye.x, 5.0F);
    EXPECT_FLOAT_EQ(other.target.z, 10.0F);
}

TEST(BookmarkSet, ApplyOutOfRangeRejected)
{
    cd::editor::BookmarkSet bs;
    cd::camera::Camera cam;
    EXPECT_FALSE(bs.apply(0, cam));   // empty set
}

TEST(BookmarkSet, RemoveCompacts)
{
    cd::editor::BookmarkSet bs;
    cd::camera::Camera cam;
    bs.add("a", cam);
    bs.add("b", cam);
    bs.add("c", cam);
    bs.remove(1);   // drop "b"
    EXPECT_EQ(bs.size(), 2u);
    EXPECT_EQ(bs.at(0)->name, "a");
    EXPECT_EQ(bs.at(1)->name, "c");
}

TEST(HierarchyView, RootOnlyWhenCollapsed)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    auto root = scene.create_node();
    auto child = scene.create_node();
    scene.attach(child, root);

    cd::editor::HierarchyView hv;
    auto rows = hv.visible_order(scene);
    EXPECT_EQ(rows.size(), 1u);   // root only — collapsed
    EXPECT_EQ(rows[0].first, root);
    EXPECT_EQ(rows[0].second, 0u);
}

TEST(HierarchyView, ExpandShowsChildren)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    auto root = scene.create_node();
    auto child = scene.create_node();
    scene.attach(child, root);

    cd::editor::HierarchyView hv;
    hv.expand(root);
    auto rows = hv.visible_order(scene);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].second, 0u);
    EXPECT_EQ(rows[1].first, child);
    EXPECT_EQ(rows[1].second, 1u);
}

TEST(HierarchyView, ToggleFlipsExpansion)
{
    cd::editor::HierarchyView hv;
    cd::ecs::Entity e { 5, 1 };
    EXPECT_FALSE(hv.is_expanded(e));
    hv.toggle(e);
    EXPECT_TRUE(hv.is_expanded(e));
    hv.toggle(e);
    EXPECT_FALSE(hv.is_expanded(e));
}

TEST(MenuBar, AddMenuIncrementsSize)
{
    cd::editor::MenuBar bar;
    bar.add_menu("File");
    bar.add_menu("Edit");
    EXPECT_EQ(bar.size(), 2u);
}

TEST(MenuBar, AddItemAccumulatesChildren)
{
    cd::editor::MenuBar bar;
    auto file = bar.add_menu("File");
    file.add_item("Open", [] {});
    file.add_separator();
    file.add_item("Save", [] {});
    ASSERT_EQ(bar.menus().size(), 1u);
    EXPECT_EQ(bar.menus()[0]->children.size(), 3u);
    EXPECT_EQ(bar.menus()[0]->children[1]->kind, cd::editor::MenuItem::Kind::kSeparator);
}

TEST(MenuBar, ActionInvokesCallback)
{
    cd::editor::MenuBar bar;
    int hits = 0;
    auto file = bar.add_menu("File");
    file.add_item("Hit", [&] { ++hits; });
    ASSERT_EQ(bar.menus()[0]->children.size(), 1u);
    auto& item = *bar.menus()[0]->children[0];
    item.action();
    EXPECT_EQ(hits, 1);
}

TEST(MenuBar, SubmenuNests)
{
    cd::editor::MenuBar bar;
    auto file = bar.add_menu("File");
    auto recent = file.add_submenu("Recent");
    recent.add_item("level1.scene", [] {});
    recent.add_item("level2.scene", [] {});
    EXPECT_EQ(bar.menus()[0]->children.size(), 1u);   // submenu
    EXPECT_EQ(bar.menus()[0]->children[0]->children.size(), 2u);   // grandchildren
}

TEST(PreferencesStore, SetAndGetBool)
{
    cd::editor::PreferencesStore p;
    p.set("show_grid", true);
    EXPECT_TRUE(p.has("show_grid"));
    auto v = p.get_bool("show_grid");
    ASSERT_TRUE(v.has_value());
    EXPECT_TRUE(*v);
}

TEST(PreferencesStore, IntAndDoubleAndString)
{
    cd::editor::PreferencesStore p;
    p.set("font_size", std::int64_t { 14 });
    p.set("ui_scale", 1.25);
    p.set("theme", std::string { "dark" });
    EXPECT_EQ(p.get_int("font_size").value_or(0), 14);
    EXPECT_DOUBLE_EQ(p.get_double("ui_scale").value_or(0.0), 1.25);
    EXPECT_EQ(p.get_string("theme").value_or(""), "dark");
}

TEST(PreferencesStore, TypeMismatchReturnsNullopt)
{
    cd::editor::PreferencesStore p;
    p.set("font_size", std::int64_t { 14 });
    EXPECT_FALSE(p.get_bool("font_size").has_value());
    EXPECT_FALSE(p.get_string("font_size").has_value());
}

TEST(PreferencesStore, RemoveAndClear)
{
    cd::editor::PreferencesStore p;
    p.set("k1", true);
    p.set("k2", std::int64_t { 1 });
    p.remove("k1");
    EXPECT_FALSE(p.has("k1"));
    EXPECT_EQ(p.size(), 1u);
    p.clear();
    EXPECT_EQ(p.size(), 0u);
}

// ---- phase1091 — CompositeCommand --------------------------------------------

namespace
{
/// Order-recording probe command: notes its id on apply/revert.
class ProbeCommand final : public cd::editor::ICommand
{
public:
    ProbeCommand(int id, std::vector<int>& log) : id_ { id }, log_ { &log } {}
    void apply() override { log_->push_back(id_); }
    void revert() override { log_->push_back(-id_); }
    [[nodiscard]] std::string_view label() const noexcept override { return "Probe"; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }

private:
    int id_;
    std::vector<int>* log_;
};

TEST(CompositeCommand, AppliesForwardRevertsReverse)
{
    std::vector<int> log;
    auto comp = std::make_unique<cd::editor::CompositeCommand>("Group move");
    comp->add(std::make_unique<ProbeCommand>(1, log));
    comp->add(std::make_unique<ProbeCommand>(2, log));
    comp->add(std::make_unique<ProbeCommand>(3, log));
    EXPECT_EQ(comp->size(), 3u);

    cd::editor::EditHistory hist;
    hist.push(std::move(comp));                       // applies 1,2,3
    ASSERT_EQ(log, (std::vector<int> { 1, 2, 3 }));

    EXPECT_TRUE(hist.undo());                          // reverts 3,2,1
    ASSERT_EQ(log, (std::vector<int> { 1, 2, 3, -3, -2, -1 }));

    EXPECT_TRUE(hist.redo());                          // re-applies 1,2,3
    ASSERT_EQ(log, (std::vector<int> { 1, 2, 3, -3, -2, -1, 1, 2, 3 }));
}

TEST(CompositeCommand, GroupTranslateIsOneUndoStep)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    const auto a = scene.create_node();
    const auto b = scene.create_node();
    cd::editor::EditHistory hist;

    auto comp = std::make_unique<cd::editor::CompositeCommand>("Translate 2");
    comp->add(std::make_unique<cd::editor::TranslateCommand>(
        scene, a, cd::math::Vec3f { 1.0F, 0.0F, 0.0F }));
    comp->add(std::make_unique<cd::editor::TranslateCommand>(
        scene, b, cd::math::Vec3f { 0.0F, 2.0F, 0.0F }));
    hist.push(std::move(comp));

    EXPECT_FLOAT_EQ(scene.local(a)->value.position.x, 1.0F);
    EXPECT_FLOAT_EQ(scene.local(b)->value.position.y, 2.0F);

    EXPECT_TRUE(hist.undo());  // ONE undo unwinds both
    EXPECT_FLOAT_EQ(scene.local(a)->value.position.x, 0.0F);
    EXPECT_FLOAT_EQ(scene.local(b)->value.position.y, 0.0F);
    EXPECT_FALSE(hist.undo()) << "group must be a single history entry";
}

TEST(CompositeCommand, EmptyAndNullChildrenAreSafe)
{
    auto comp = std::make_unique<cd::editor::CompositeCommand>();
    comp->add(nullptr);
    EXPECT_TRUE(comp->empty());
    cd::editor::EditHistory hist;
    hist.push(std::move(comp));   // applying an empty composite is a no-op
    EXPECT_TRUE(hist.undo());
}

}  // namespace

// ---- phase-editor-70pct — gap-close: EditHistory, SelectionSet, CommandPalette, HierarchyView ----

namespace
{

/// Minimal ICommand that increments/decrements a counter.
/// Local re-declaration (CounterCommand above lives in a separate anon namespace).
class GapCounterCmd final : public cd::editor::ICommand
{
public:
    explicit GapCounterCmd(int* sink) noexcept : sink_(sink) {}
    void apply()  override { ++(*sink_); }
    void revert() override { --(*sink_); }
    [[nodiscard]] std::string_view label() const noexcept override { return "GapCounter"; }
    [[nodiscard]] std::size_t byte_size() const noexcept override { return sizeof(*this); }
private:
    int* sink_;
};

// ---------------------------------------------------------------------------
// EditHistory — edge / negative
// ---------------------------------------------------------------------------

TEST(EditHistory, UndoOnEmptyReturnsFalse)
{
    cd::editor::EditHistory hist;
    EXPECT_FALSE(hist.undo());
    EXPECT_EQ(hist.undo_depth(), 0U);
}

TEST(EditHistory, RedoOnEmptyReturnsFalse)
{
    cd::editor::EditHistory hist;
    int counter = 0;
    hist.push(std::make_unique<GapCounterCmd>(&counter));
    EXPECT_TRUE(hist.undo());
    EXPECT_TRUE(hist.redo());
    EXPECT_FALSE(hist.redo());  // nothing left on redo stack
}

TEST(EditHistory, ByteBudgetEvictsOldest)
{
    // Set the budget to exactly the size of one GapCounterCmd so the second
    // push triggers an eviction of the oldest entry.
    cd::editor::EditHistory::Config cfg;
    cfg.max_entries = 128;
    cfg.max_bytes   = sizeof(GapCounterCmd);
    cd::editor::EditHistory hist { cfg };

    int counter = 0;
    hist.push(std::make_unique<GapCounterCmd>(&counter));  // fills budget
    hist.push(std::make_unique<GapCounterCmd>(&counter));  // evicts oldest

    // Only 1 entry must survive on the undo stack.
    EXPECT_EQ(hist.undo_depth(), 1U);
}

TEST(EditHistory, NullPushIsNoOp)
{
    cd::editor::EditHistory hist;
    hist.push(nullptr);  // must not crash
    EXPECT_FALSE(hist.can_undo());
    EXPECT_EQ(hist.bytes_in_use(), 0U);
}

TEST(EditHistory, NextLabelEmptyWhenStacksEmpty)
{
    const cd::editor::EditHistory hist;
    EXPECT_TRUE(hist.next_undo_label().empty());
    EXPECT_TRUE(hist.next_redo_label().empty());
}

TEST(EditHistory, NextRedoLabelPopulatedAfterUndo)
{
    cd::editor::EditHistory hist;
    int counter = 0;
    hist.push(std::make_unique<GapCounterCmd>(&counter));
    hist.undo();
    EXPECT_EQ(hist.next_redo_label(), "GapCounter");
}

// ---------------------------------------------------------------------------
// SelectionSet — edge / negative
// ---------------------------------------------------------------------------

TEST(SelectionSet, SortedOrderGuaranteed)
{
    // entries() must remain sorted by entity id ascending regardless of add order.
    cd::editor::SelectionSet s;
    const cd::ecs::Entity a { 10, 1 };
    const cd::ecs::Entity b { 2,  1 };
    const cd::ecs::Entity c { 7,  1 };
    s.add(a);
    s.add(b);
    s.add(c);

    const auto& ents = s.entries();
    ASSERT_EQ(ents.size(), 3U);
    EXPECT_LT(ents[0].id, ents[1].id);
    EXPECT_LT(ents[1].id, ents[2].id);
}

TEST(SelectionSet, RemoveAbsentEntityIsNoOp)
{
    cd::editor::SelectionSet s;
    const cd::ecs::Entity a { 1, 1 };
    s.add(a);
    s.remove(cd::ecs::Entity { 99, 1 });  // not in set — must not crash
    EXPECT_EQ(s.size(), 1U);
    EXPECT_TRUE(s.contains(a));
}

TEST(SelectionSet, RemoveLastEntityClearsPrimary)
{
    cd::editor::SelectionSet s;
    const cd::ecs::Entity a { 5, 1 };
    s.add(a);
    s.remove(a);
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(s.primary().is_valid());
}

TEST(SelectionSet, ReAddAfterRemoveRestoresMembership)
{
    cd::editor::SelectionSet s;
    const cd::ecs::Entity a { 3, 1 };
    s.add(a);
    s.remove(a);
    ASSERT_FALSE(s.contains(a));
    s.add(a);
    EXPECT_TRUE(s.contains(a));
    EXPECT_EQ(s.primary(), a);
}

// ---------------------------------------------------------------------------
// CommandPalette — edge / negative
// ---------------------------------------------------------------------------

TEST(CommandPalette, InvokeOutOfBoundsReturnsFalse)
{
    cd::editor::CommandPalette p;
    EXPECT_FALSE(p.invoke(0));   // empty palette — index 0 is OOB
    p.register_command(1, "Open", [] {});
    EXPECT_FALSE(p.invoke(1));   // only index 0 is valid after one registration
}

TEST(CommandPalette, InvokeNullActionReturnsFalse)
{
    cd::editor::CommandPalette p;
    p.register_command(42, "NoOp", std::function<void()> {});
    // Null / empty action — invoke must not crash and must return false.
    EXPECT_FALSE(p.invoke(0));
}

TEST(CommandPalette, ScoreRanksWordBoundaryHigher)
{
    // "of" in "Open File": 'O' is at word start (boundary boost) + 'f' starts "File"
    // (another boundary boost).  "o_xxx_f" has no word-boundary matches.
    cd::editor::CommandPalette p;
    p.register_command(1, "Open File", [] {});  // word-boundary double-boost
    p.register_command(2, "o_xxx_f",   [] {});  // subsequence match, no boundary

    const auto hits = p.filter("of");
    ASSERT_EQ(hits.size(), 2U);
    // "Open File" must rank first (hits[0] == index 0 in the registry).
    EXPECT_EQ(hits[0], 0U);
}

TEST(CommandPalette, EmptyPaletteFilterReturnsEmpty)
{
    const cd::editor::CommandPalette p;
    EXPECT_TRUE(p.filter("").empty());
    EXPECT_TRUE(p.filter("x").empty());
}

// ---------------------------------------------------------------------------
// HierarchyView — edge / negative
// ---------------------------------------------------------------------------

TEST(HierarchyView, MultipleRootsAllVisibleAtDepthZero)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    const auto r1 = scene.create_node();
    const auto r2 = scene.create_node();
    const auto r3 = scene.create_node();
    static_cast<void>(r1);
    static_cast<void>(r2);
    static_cast<void>(r3);

    cd::editor::HierarchyView hv;
    const auto rows = hv.visible_order(scene);
    // 3 independent roots, none expanded → 3 rows all at depth 0.
    EXPECT_EQ(rows.size(), 3U);
    for (const auto& [e, d] : rows) EXPECT_EQ(d, 0U);
}

TEST(HierarchyView, CollapseHidesChildren)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    const auto root  = scene.create_node();
    const auto child = scene.create_node();
    scene.attach(child, root);

    cd::editor::HierarchyView hv;
    hv.expand(root);
    ASSERT_EQ(hv.visible_order(scene).size(), 2U);

    hv.collapse(root);
    EXPECT_EQ(hv.visible_order(scene).size(), 1U);
}

TEST(HierarchyView, ClearCollapsesAll)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    const auto root  = scene.create_node();
    const auto child = scene.create_node();
    scene.attach(child, root);

    cd::editor::HierarchyView hv;
    hv.expand(root);
    ASSERT_EQ(hv.expanded_count(), 1U);

    hv.clear();
    EXPECT_EQ(hv.expanded_count(), 0U);
    EXPECT_EQ(hv.visible_order(scene).size(), 1U);  // root only after clear
}

TEST(HierarchyView, DepthThreeNestedStructure)
{
    cd::ecs::World world;
    cd::scene::Scene scene { world };
    const auto root = scene.create_node();
    const auto mid  = scene.create_node();
    const auto leaf = scene.create_node();
    scene.attach(mid,  root);
    scene.attach(leaf, mid);

    cd::editor::HierarchyView hv;
    hv.expand(root);
    hv.expand(mid);

    const auto rows = hv.visible_order(scene);
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].second, 0U);  // root at depth 0
    EXPECT_EQ(rows[1].second, 1U);  // mid  at depth 1
    EXPECT_EQ(rows[2].second, 2U);  // leaf at depth 2
}

}  // namespace
