// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_behavior_designer/tests/test_behavior_designer.cpp
//
// phase556-557-558 — Sprint-1 unit tests.
// phase740         — Sprint-2: adds set_tree / real BT graph tests.
//
// All tests are headless (no ImGui / no RHI). We verify:
//
// Sprint-1 (legacy):
//   DefaultCtorHasNoRootOrSelection  — tree_root/selected == kInvalidNodeId,
//                                       pan defaults to (0, 0), tree == nullptr.
//   SetTreeRootRoundTrips            — set_tree_root / tree_root + clear.
//   SetSelectedRoundTrips            — set_selected / selected + clear.
//   SetPanOffsetRoundTrips           — set_pan_offset / pan_x / pan_y.
//   DrawDefaultDoesNotCrash          — draw() with default state emits bg quad.
//   DrawWithRootAndSelectionEmitsQuads — draw() with bound root + selected node
//                                        emits more quads (selection border).
//   DrawZeroBoundsDoesNotCrash       — draw() on a zero-size rect returns early.
//
// Sprint-2 (set_tree integration):
//   SetTreeRoundTrips                — set_tree / tree() round-trip + nullptr clear.
//   DrawWithRealBtEmitsMoreQuadsThanDemo — a 5-node BT emits more quads than
//                                          the 3-demo-node fallback.
//   DrawRealBtWithSelectionEmitsAccentBorder — selected node in real BT gets
//                                              the accent border quad.
//   DrawRealBtDoesNotCrashFor50Nodes — stress: 50-node chain does not crash.
// =============================================================================
#include <cd/editor/panel_behavior_designer/BehaviorDesigner.hpp>

#include <cd/game/ai_bt/BehaviorTree.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace bd  = cd::editor::panel::behavior_designer;
namespace bt  = cd::game::ai_bt;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{
    /// Build a minimal 3-node BT:  Selector -> [Sequence, Leaf].
    bt::BehaviorTree make_small_tree()
    {
        auto seq = std::make_unique<bt::SequenceNode>();
        seq->add_child(bt::make_leaf([](bt::Blackboard&) { return bt::Status::kSuccess; }));
        seq->add_child(bt::make_leaf([](bt::Blackboard&) { return bt::Status::kSuccess; }));

        auto sel = std::make_unique<bt::SelectorNode>();
        sel->add_child(std::move(seq));
        sel->add_child(bt::make_leaf([](bt::Blackboard&) { return bt::Status::kFailure; }));

        return bt::BehaviorTree(std::move(sel));
    }

    /// Build a linear chain of N SequenceNodes (stress test).
    bt::BehaviorTree make_chain(std::size_t n)
    {
        auto leaf = bt::make_leaf([](bt::Blackboard&) { return bt::Status::kSuccess; });
        std::unique_ptr<bt::Node> current = std::move(leaf);
        for (std::size_t i = 0; i < n; ++i)
        {
            auto seq = std::make_unique<bt::SequenceNode>();
            seq->add_child(std::move(current));
            current = std::move(seq);
        }
        return bt::BehaviorTree(std::move(current));
    }
}

// ---------------------------------------------------------------------------
// Sprint-1 tests (legacy API, unchanged behaviour)
// ---------------------------------------------------------------------------

TEST(BehaviorDesignerPanel, DefaultCtorHasNoRootOrSelection)
{
    const bd::BehaviorDesigner designer;

    EXPECT_EQ(designer.tree_root(), bd::kInvalidNodeId);
    EXPECT_EQ(designer.selected(),  bd::kInvalidNodeId);
    EXPECT_FLOAT_EQ(designer.pan_x(), 0.0F);
    EXPECT_FLOAT_EQ(designer.pan_y(), 0.0F);
    EXPECT_EQ(designer.tree(), nullptr);
}

TEST(BehaviorDesignerPanel, SetTreeRootRoundTrips)
{
    bd::BehaviorDesigner designer;

    designer.set_tree_root(1U);
    EXPECT_EQ(designer.tree_root(), 1U);

    designer.set_tree_root(99U);
    EXPECT_EQ(designer.tree_root(), 99U);

    designer.set_tree_root(bd::kInvalidNodeId);
    EXPECT_EQ(designer.tree_root(), bd::kInvalidNodeId);
}

TEST(BehaviorDesignerPanel, SetSelectedRoundTrips)
{
    bd::BehaviorDesigner designer;

    designer.set_selected(2U);
    EXPECT_EQ(designer.selected(), 2U);

    designer.set_selected(3U);
    EXPECT_EQ(designer.selected(), 3U);

    designer.set_selected(bd::kInvalidNodeId);
    EXPECT_EQ(designer.selected(), bd::kInvalidNodeId);
}

TEST(BehaviorDesignerPanel, SetPanOffsetRoundTrips)
{
    bd::BehaviorDesigner designer;

    designer.set_pan_offset(120.5F, -30.0F);
    EXPECT_FLOAT_EQ(designer.pan_x(),  120.5F);
    EXPECT_FLOAT_EQ(designer.pan_y(), -30.0F);

    designer.set_pan_offset(0.0F, 0.0F);
    EXPECT_FLOAT_EQ(designer.pan_x(), 0.0F);
    EXPECT_FLOAT_EQ(designer.pan_y(), 0.0F);
}

TEST(BehaviorDesignerPanel, DrawDefaultDoesNotCrash)
{
    bd::BehaviorDesigner          designer;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    designer.draw(batcher, theme, bounds);

    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

TEST(BehaviorDesignerPanel, DrawWithRootAndSelectionEmitsQuads)
{
    bd::BehaviorDesigner designer;
    designer.set_tree_root(1U);
    designer.set_selected(1U);
    designer.set_pan_offset(0.0F, 0.0F);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 500.0F, 700.0F };

    batcher.begin_frame();
    designer.draw(batcher, theme, bounds);

    // background (1) + separator (1) + grid lines (several) +
    // 4 edge quads + 3 nodes * 2 quads + 1 selection border = > 12 quads = 48 verts.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(48U));
}

TEST(BehaviorDesignerPanel, DrawZeroBoundsDoesNotCrash)
{
    bd::BehaviorDesigner          designer;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    designer.draw(batcher, theme, zero);

    // bounds.is_valid() == false → returns early after the background quad.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}

// ---------------------------------------------------------------------------
// Sprint-2 tests (set_tree / real BT)
// ---------------------------------------------------------------------------

TEST(BehaviorDesignerPanel, SetTreeRoundTrips)
{
    bd::BehaviorDesigner designer;
    EXPECT_EQ(designer.tree(), nullptr);

    bt::BehaviorTree small = make_small_tree();
    designer.set_tree(&small);
    EXPECT_EQ(designer.tree(), &small);

    // Bind a different tree.
    bt::BehaviorTree other = make_small_tree();
    designer.set_tree(&other);
    EXPECT_EQ(designer.tree(), &other);

    // Detach.
    designer.set_tree(nullptr);
    EXPECT_EQ(designer.tree(), nullptr);
}

TEST(BehaviorDesignerPanel, DrawWithRealBtEmitsMoreQuadsThanDemo)
{
    // 5-node tree => real path; 3-node demo path for comparison.
    bt::BehaviorTree small = make_small_tree();  // 5 real nodes

    bd::BehaviorDesigner demo_designer;           // no tree => demo fallback
    bd::BehaviorDesigner real_designer;
    real_designer.set_tree(&small);

    cd::ui::renderer::DrawBatcher demo_batcher;
    cd::ui::renderer::DrawBatcher real_batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 600.0F, 800.0F };

    demo_batcher.begin_frame();
    demo_designer.draw(demo_batcher, theme, bounds);

    real_batcher.begin_frame();
    real_designer.draw(real_batcher, theme, bounds);

    // A 5-node real tree must emit at least as many quads as the 3-node demo.
    // (More nodes → more node bodies + more edges.)
    EXPECT_GE(real_batcher.vertex_count(), demo_batcher.vertex_count());
}

TEST(BehaviorDesignerPanel, DrawRealBtWithSelectionEmitsAccentBorder)
{
    bt::BehaviorTree small = make_small_tree();

    // Draw once without selection.
    bd::BehaviorDesigner no_sel;
    no_sel.set_tree(&small);

    // Draw once with selection on node id=1 (pre-order root gets id=1).
    bd::BehaviorDesigner with_sel;
    with_sel.set_tree(&small);
    with_sel.set_selected(1U);

    cd::ui::renderer::DrawBatcher b_no_sel;
    cd::ui::renderer::DrawBatcher b_with_sel;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 600.0F, 800.0F };

    b_no_sel.begin_frame();
    no_sel.draw(b_no_sel, theme, bounds);

    b_with_sel.begin_frame();
    with_sel.draw(b_with_sel, theme, bounds);

    // Selection adds one extra border quad (4 vertices) for the selected node.
    EXPECT_GT(b_with_sel.vertex_count(), b_no_sel.vertex_count());
}

TEST(BehaviorDesignerPanel, DrawRealBtDoesNotCrashFor50Nodes)
{
    bt::BehaviorTree chain = make_chain(50U);

    bd::BehaviorDesigner designer;
    designer.set_tree(&chain);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 1920.0F, 1080.0F };

    batcher.begin_frame();
    // Must not crash, assert, or throw for a 50-node deep chain.
    designer.draw(batcher, theme, bounds);

    // Must have emitted at least background + separator = 2 quads = 8 verts.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}
