// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_behavior_designer/tests/test_behavior_designer.cpp
//
// phase556-557-558 — unit tests for cd::editor::panel::behavior_designer::BehaviorDesigner.
//
// All tests are headless (no ImGui / no RHI). We verify:
//   * DefaultCtorHasNoRootOrSelection  — tree_root/selected == kInvalidNodeId,
//                                         pan defaults to (0, 0).
//   * SetTreeRootRoundTrips            — set_tree_root / tree_root + clear.
//   * SetSelectedRoundTrips            — set_selected / selected + clear.
//   * SetPanOffsetRoundTrips           — set_pan_offset / pan_x / pan_y.
//   * DrawDefaultDoesNotCrash          — draw() with default state emits bg quad.
//   * DrawWithRootAndSelectionEmitsQuads — draw() with bound root + selected node
//                                          emits more quads (selection border).
//   * DrawZeroBoundsDoesNotCrash       — draw() on a zero-size rect returns early.
// =============================================================================
#include <cd/editor/panel_behavior_designer/BehaviorDesigner.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <gtest/gtest.h>

namespace bd = cd::editor::panel::behavior_designer;

// ---------------------------------------------------------------------------
// TEST(BehaviorDesignerPanel, DefaultCtorHasNoRootOrSelection)
// ---------------------------------------------------------------------------
TEST(BehaviorDesignerPanel, DefaultCtorHasNoRootOrSelection)
{
    const bd::BehaviorDesigner designer;

    EXPECT_EQ(designer.tree_root(), bd::kInvalidNodeId);
    EXPECT_EQ(designer.selected(),  bd::kInvalidNodeId);
    EXPECT_FLOAT_EQ(designer.pan_x(), 0.0F);
    EXPECT_FLOAT_EQ(designer.pan_y(), 0.0F);
}

// ---------------------------------------------------------------------------
// TEST(BehaviorDesignerPanel, SetTreeRootRoundTrips)
// ---------------------------------------------------------------------------
TEST(BehaviorDesignerPanel, SetTreeRootRoundTrips)
{
    bd::BehaviorDesigner designer;

    designer.set_tree_root(1U);
    EXPECT_EQ(designer.tree_root(), 1U);

    // Rebind to a different root.
    designer.set_tree_root(99U);
    EXPECT_EQ(designer.tree_root(), 99U);

    // Clear with the sentinel.
    designer.set_tree_root(bd::kInvalidNodeId);
    EXPECT_EQ(designer.tree_root(), bd::kInvalidNodeId);
}

// ---------------------------------------------------------------------------
// TEST(BehaviorDesignerPanel, SetSelectedRoundTrips)
// ---------------------------------------------------------------------------
TEST(BehaviorDesignerPanel, SetSelectedRoundTrips)
{
    bd::BehaviorDesigner designer;

    designer.set_selected(2U);
    EXPECT_EQ(designer.selected(), 2U);

    // Re-select a different node.
    designer.set_selected(3U);
    EXPECT_EQ(designer.selected(), 3U);

    // Clear selection.
    designer.set_selected(bd::kInvalidNodeId);
    EXPECT_EQ(designer.selected(), bd::kInvalidNodeId);
}

// ---------------------------------------------------------------------------
// TEST(BehaviorDesignerPanel, SetPanOffsetRoundTrips)
// ---------------------------------------------------------------------------
TEST(BehaviorDesignerPanel, SetPanOffsetRoundTrips)
{
    bd::BehaviorDesigner designer;

    designer.set_pan_offset(120.5F, -30.0F);
    EXPECT_FLOAT_EQ(designer.pan_x(),  120.5F);
    EXPECT_FLOAT_EQ(designer.pan_y(), -30.0F);

    // Reset to zero.
    designer.set_pan_offset(0.0F, 0.0F);
    EXPECT_FLOAT_EQ(designer.pan_x(), 0.0F);
    EXPECT_FLOAT_EQ(designer.pan_y(), 0.0F);
}

// ---------------------------------------------------------------------------
// TEST(BehaviorDesignerPanel, DrawDefaultDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(BehaviorDesignerPanel, DrawDefaultDoesNotCrash)
{
    bd::BehaviorDesigner          designer;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 400.0F, 600.0F };

    batcher.begin_frame();
    // Must not throw or crash with default (no-root, no-selection) state.
    designer.draw(batcher, theme, bounds);

    // Background quad must have been emitted (at least one command).
    EXPECT_GE(batcher.command_count(), static_cast<std::size_t>(1U));
}

// ---------------------------------------------------------------------------
// TEST(BehaviorDesignerPanel, DrawWithRootAndSelectionEmitsQuads)
// ---------------------------------------------------------------------------
TEST(BehaviorDesignerPanel, DrawWithRootAndSelectionEmitsQuads)
{
    bd::BehaviorDesigner designer;

    // Bind a root and select one of the 3 demo nodes (id = 1 = Root).
    designer.set_tree_root(1U);
    designer.set_selected(1U);
    designer.set_pan_offset(0.0F, 0.0F);

    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   bounds { 0.0F, 0.0F, 500.0F, 700.0F };

    batcher.begin_frame();
    designer.draw(batcher, theme, bounds);

    // Expect at minimum:
    //   background (1) + separator (1) + grid lines (several) +
    //   4 edge quads + 3 nodes * 2 quads (body + label) + 1 selection border
    //   = well over 12 quads = 48 verts.
    EXPECT_GE(batcher.vertex_count(), static_cast<std::size_t>(48U));
}

// ---------------------------------------------------------------------------
// TEST(BehaviorDesignerPanel, DrawZeroBoundsDoesNotCrash)
// ---------------------------------------------------------------------------
TEST(BehaviorDesignerPanel, DrawZeroBoundsDoesNotCrash)
{
    bd::BehaviorDesigner          designer;
    cd::ui::renderer::DrawBatcher batcher;
    const cd::ui::widgets::Theme  theme {};
    const cd::ui::widgets::Rect   zero { 0.0F, 0.0F, 0.0F, 0.0F };

    batcher.begin_frame();
    // Must not throw or crash for a zero-size rect.
    designer.draw(batcher, theme, zero);

    // bounds.is_valid() == false → returns early after the background quad.
    // Node / grid quads must NOT be emitted; vertex count stays very low.
    EXPECT_LE(batcher.vertex_count(), static_cast<std::size_t>(8U));
}
