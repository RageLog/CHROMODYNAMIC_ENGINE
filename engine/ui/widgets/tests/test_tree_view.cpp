// =============================================================================
// CHROMODYNAMIC — cd::ui::widgets::TreeView tests
//
// Phase 683 / M13 W5B. Seven test cases covering:
//   1. Default construction — zero nodes, no selection.
//   2. set_nodes + root_count round-trip.
//   3. toggle_expand changes expanded state.
//   4. draw emits visible rows only (collapsed children skipped).
//   5. simulate_click selects correct node.
//   6. Deep nesting (5 levels) — draw emits correct row count.
//   7. Out-of-range toggle_expand is a safe no-op.
//
// All tests are CPU-only — no GPU resources touched, no font loaded.
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/TreeView.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace w = cd::ui::widgets;
namespace r = cd::ui::renderer;

namespace
{

constexpr w::Rect kBounds { 0.0F, 0.0F, 300.0F, 400.0F };

w::Theme make_theme() noexcept { return w::Theme {}; }

// ---------------------------------------------------------------------------
// Build a simple 3-root / 2-child tree:
//
//   [0] Root A        (collapsed)
//     [1] Child A1
//   [2] Root B        (collapsed)
//     [3] Child B1
//   [4] Root C        (no children, collapsed)
// ---------------------------------------------------------------------------
std::vector<w::Node> simple_tree()
{
    std::vector<w::Node> nodes(5U);

    nodes[0].id            = "root_a";
    nodes[0].label         = "Root A";
    nodes[0].child_indices = { 1U };
    nodes[0].expanded      = false;

    nodes[1].id            = "child_a1";
    nodes[1].label         = "Child A1";
    nodes[1].expanded      = false;

    nodes[2].id            = "root_b";
    nodes[2].label         = "Root B";
    nodes[2].child_indices = { 3U };
    nodes[2].expanded      = false;

    nodes[3].id            = "child_b1";
    nodes[3].label         = "Child B1";
    nodes[3].expanded      = false;

    nodes[4].id            = "root_c";
    nodes[4].label         = "Root C";
    nodes[4].expanded      = false;

    return nodes;
}

// ---------------------------------------------------------------------------
// Build a 5-level deep chain: 0 -> 1 -> 2 -> 3 -> 4, all expanded.
// ---------------------------------------------------------------------------
std::vector<w::Node> deep_tree_5()
{
    std::vector<w::Node> nodes(5U);
    for (std::size_t i = 0U; i < 4U; ++i)
    {
        nodes[i].id            = "n" + std::to_string(i);
        nodes[i].label         = "Node " + std::to_string(i);
        nodes[i].child_indices = { i + 1U };
        nodes[i].expanded      = true;   // all expanded
    }
    nodes[4U].id    = "n4";
    nodes[4U].label = "Node 4";
    nodes[4U].expanded = false;

    return nodes;
}

}  // namespace

// =============================================================================
// Case 1: Default construction — zero nodes, no selection.
// =============================================================================
TEST(UiWidgetsTreeView, DefaultConstruction)
{
    const w::TreeView tv;
    EXPECT_EQ(tv.node_count(),  0U);
    EXPECT_EQ(tv.root_count(),  0U);
    EXPECT_FALSE(tv.selected_node_index().has_value());
}

// =============================================================================
// Case 2: set_nodes + root_count round-trip.
// =============================================================================
TEST(UiWidgetsTreeView, SetNodesRoundTrip)
{
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    EXPECT_EQ(tv.node_count(), 5U);
    // Roots: nodes 0, 2, 4 (nodes 1 and 3 appear as children).
    EXPECT_EQ(tv.root_count(), 3U);

    // Node data preserved.
    EXPECT_EQ(tv.node(0U).id,    "root_a");
    EXPECT_EQ(tv.node(0U).label, "Root A");
    EXPECT_EQ(tv.node(1U).id,    "child_a1");

    // Selection cleared.
    EXPECT_FALSE(tv.selected_node_index().has_value());
}

// =============================================================================
// Case 3: toggle_expand changes expanded state.
// =============================================================================
TEST(UiWidgetsTreeView, ToggleExpandChangesState)
{
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    // Initially collapsed.
    EXPECT_FALSE(tv.node(0U).expanded);

    tv.toggle_expand(0U);
    EXPECT_TRUE(tv.node(0U).expanded);

    tv.toggle_expand(0U);
    EXPECT_FALSE(tv.node(0U).expanded);
}

// =============================================================================
// Case 4: draw emits visible rows only (collapsed children skipped).
//
// With the simple_tree (all collapsed) the visible set is the 3 roots:
// {0, 2, 4}. Each root produces 1 background quad.
// After expanding root A (node 0) the visible set is {0, 1, 2, 4} = 4 rows.
//
// Each visible row emits:
//   1 background quad
//   + 1 highlight quad (if selected — skipped here)
//   + depth indent-guide quads (roots have depth 0, children depth 1 -> 1 guide)
//
// All-collapsed (3 roots, depth 0 each): 3 * 1 = 3 quads -> 12 vertices.
// After expanding root A: 3 root rows (0 guides each) + 1 child row (1 guide)
//   = 3*1 + 1*1 + 1*1 = 5 quads -> 20 vertices.
// =============================================================================
TEST(UiWidgetsTreeView, DrawEmitsVisibleNodesOnly)
{
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    r::DrawBatcher batcher;

    // --- Collapsed (3 visible roots) ----------------------------------------
    batcher.begin_frame();
    tv.draw(batcher, make_theme(), kBounds);

    // 3 root rows * 1 bg quad each = 3 quads = 12 vertices.
    EXPECT_EQ(batcher.vertex_count(), 3U * 4U);

    // --- Expand root A (adds child A1, depth 1) ------------------------------
    tv.toggle_expand(0U);

    batcher.begin_frame();
    tv.draw(batcher, make_theme(), kBounds);

    // 3 root bg + 1 child bg + 1 guide for child = 5 quads = 20 vertices.
    EXPECT_EQ(batcher.vertex_count(), 5U * 4U);
}

// =============================================================================
// Case 5: simulate_click selects the correct node.
// =============================================================================
TEST(UiWidgetsTreeView, SimulateClickSelectsCorrectNode)
{
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    // With all collapsed, visible rows are roots [0, 2, 4] in that order.
    // Row 0 -> node 0,  row 1 -> node 2,  row 2 -> node 4.

    // Click in the middle of row 1 (node index 2 / "Root B").
    const float click_y = kBounds.y
                        + 1.0F * w::TreeView::kRowHeight
                        + w::TreeView::kRowHeight * 0.5F;

    tv.simulate_click(kBounds.x + 50.0F, click_y, kBounds);

    ASSERT_TRUE(tv.selected_node_index().has_value());
    EXPECT_EQ(*tv.selected_node_index(), 2U);

    // Click below all rows clears selection.
    const float below_y = kBounds.y
                        + 10.0F * w::TreeView::kRowHeight;  // only 3 visible rows
    tv.simulate_click(kBounds.x + 50.0F, below_y, kBounds);
    EXPECT_FALSE(tv.selected_node_index().has_value());
}

// =============================================================================
// Case 6: Deep nesting (5 levels) draws correctly.
//
// deep_tree_5: chain 0->1->2->3->4, all expanded.
// Visible rows: 5 (all nodes, depths 0..4).
//
// Quads per row:
//   depth 0: 1 bg + 0 guides = 1
//   depth 1: 1 bg + 1 guide  = 2
//   depth 2: 1 bg + 2 guides = 3
//   depth 3: 1 bg + 3 guides = 4
//   depth 4: 1 bg + 4 guides = 5
// Total quads: 1+2+3+4+5 = 15 -> 60 vertices.
// =============================================================================
TEST(UiWidgetsTreeView, DeepNestingDrawsCorrectly)
{
    w::TreeView tv;
    const auto nodes = deep_tree_5();
    tv.set_nodes(nodes);

    // 5-node chain: only node 0 is root.
    EXPECT_EQ(tv.root_count(), 1U);

    r::DrawBatcher batcher;
    batcher.begin_frame();
    tv.draw(batcher, make_theme(), kBounds);

    // sum(1..5) quads * 4 vertices each = 15 * 4 = 60.
    EXPECT_EQ(batcher.vertex_count(), 60U);
}

// =============================================================================
// Case 7: Out-of-range toggle_expand is a safe no-op.
// =============================================================================
TEST(UiWidgetsTreeView, ToggleExpandOutOfRangeIsNoOp)
{
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    // Should not crash or alter any state.
    EXPECT_NO_FATAL_FAILURE(tv.toggle_expand(999U));
    EXPECT_EQ(tv.node_count(), 5U);
    EXPECT_FALSE(tv.node(0U).expanded);
}

// =============================================================================
// Case 8: set_nodes resets the selection to empty.
// =============================================================================
TEST(UiWidgetsTreeView, SetNodesResetsSelection)
{
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    // Select a node.
    const float click_y = kBounds.y + w::TreeView::kRowHeight * 0.5F;
    tv.simulate_click(kBounds.x + 50.0F, click_y, kBounds);
    ASSERT_TRUE(tv.selected_node_index().has_value());

    // Replace nodes — selection must clear.
    tv.set_nodes(nodes);
    EXPECT_FALSE(tv.selected_node_index().has_value());
}

// =============================================================================
// Case 9: clear_selection removes the selection without altering nodes.
// =============================================================================
TEST(UiWidgetsTreeView, ClearSelectionRemovesSelectionOnly)
{
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    const float click_y = kBounds.y + w::TreeView::kRowHeight * 0.5F;
    tv.simulate_click(kBounds.x + 50.0F, click_y, kBounds);
    ASSERT_TRUE(tv.selected_node_index().has_value());

    tv.clear_selection();
    EXPECT_FALSE(tv.selected_node_index().has_value());
    // Nodes unchanged.
    EXPECT_EQ(tv.node_count(), 5U);
}

// =============================================================================
// Case 10: root_count reflects multi-root trees correctly.
// =============================================================================
TEST(UiWidgetsTreeView, RootCountCorrectForMultiRootTree)
{
    // simple_tree has roots at indices 0, 2, 4 (nodes 1 and 3 are children).
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    EXPECT_EQ(tv.root_count(), 3U);

    // A single-root tree: chain 0->1->2 (no node is unreferenced except 0).
    std::vector<w::Node> chain(3U);
    chain[0].id = "r"; chain[0].child_indices = { 1U };
    chain[1].id = "m"; chain[1].child_indices = { 2U };
    chain[2].id = "l";
    tv.set_nodes(chain);
    EXPECT_EQ(tv.root_count(), 1U);
}

// =============================================================================
// Case 11: Expanding a node with no children does not add visible rows.
// =============================================================================
TEST(UiWidgetsTreeView, ExpandLeafNodeAddsNoVisibleRows)
{
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    // Node 4 ("Root C") has no children.
    ASSERT_TRUE(tv.node(4U).child_indices.empty());

    r::DrawBatcher batcher;

    // Collapsed: 3 roots visible -> 3 quads (12 vertices).
    batcher.begin_frame();
    tv.draw(batcher, make_theme(), kBounds);
    EXPECT_EQ(batcher.vertex_count(), 3U * 4U);

    // Toggle expand on the leaf node — should still show 3 roots.
    tv.toggle_expand(4U);
    batcher.begin_frame();
    tv.draw(batcher, make_theme(), kBounds);
    EXPECT_EQ(batcher.vertex_count(), 3U * 4U);
}

// =============================================================================
// Case 12: simulate_click out-of-bounds (y below all rows) clears selection.
// =============================================================================
TEST(UiWidgetsTreeView, SimulateClickBelowAllRowsClearsSelection)
{
    w::TreeView tv;
    const auto nodes = simple_tree();
    tv.set_nodes(nodes);

    // First select something.
    const float click_y = kBounds.y + w::TreeView::kRowHeight * 0.5F;
    tv.simulate_click(kBounds.x + 50.0F, click_y, kBounds);
    ASSERT_TRUE(tv.selected_node_index().has_value());

    // Click well below the visible area.
    const float below_y = kBounds.y + 100.0F * w::TreeView::kRowHeight;
    tv.simulate_click(kBounds.x + 50.0F, below_y, kBounds);
    EXPECT_FALSE(tv.selected_node_index().has_value());
}

// =============================================================================
// Case 13: Nested expand: expanding two levels reveals grandchildren.
// =============================================================================
TEST(UiWidgetsTreeView, NestedExpandRevealsTwoLevels)
{
    // Build: 0 -> 1 -> 2 (three-level chain).
    std::vector<w::Node> nodes(3U);
    nodes[0].id = "g"; nodes[0].child_indices = { 1U }; nodes[0].expanded = false;
    nodes[1].id = "p"; nodes[1].child_indices = { 2U }; nodes[1].expanded = false;
    nodes[2].id = "c";                                   nodes[2].expanded = false;

    w::TreeView tv;
    tv.set_nodes(nodes);
    EXPECT_EQ(tv.root_count(), 1U);

    r::DrawBatcher batcher;

    // Collapsed: only root visible -> 1 quad (4 vertices).
    batcher.begin_frame();
    tv.draw(batcher, make_theme(), kBounds);
    EXPECT_EQ(batcher.vertex_count(), 1U * 4U);

    // Expand root only: root + parent visible (2 rows; parent at depth 1 -> 1 guide).
    tv.toggle_expand(0U);
    batcher.begin_frame();
    tv.draw(batcher, make_theme(), kBounds);
    // root (1 quad) + parent (1 bg + 1 guide = 2 quads) = 3 quads = 12 vertices.
    EXPECT_EQ(batcher.vertex_count(), 3U * 4U);

    // Expand parent too: root + parent + child visible.
    // root depth 0 (1 quad), parent depth 1 (2 quads), child depth 2 (3 quads) = 6 quads.
    tv.toggle_expand(1U);
    batcher.begin_frame();
    tv.draw(batcher, make_theme(), kBounds);
    EXPECT_EQ(batcher.vertex_count(), 6U * 4U);
}
