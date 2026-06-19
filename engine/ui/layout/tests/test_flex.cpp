// =============================================================================
// CHROMODYNAMIC — cd::ui::layout Flex solver tests
//
// Yoga-parity smoke tests. Each case sets up a small tree, runs the
// solver, and asserts the computed rects match the Yoga reference. The
// reference values were computed offline against Yoga 1.19 (web playground
// + sanity check) and pinned in this file.
//
// Phase 1.0 coverage:
//   - Row + column directions
//   - justify-content: flex-start / center / flex-end / space-between /
//                       space-around / space-evenly
//   - align-items: flex-start / center / flex-end / stretch
//   - flex-grow distribution (1:1, 2:1:1, etc.)
//   - flex-shrink when content overflows
//   - padding on parent
//   - gap_main between siblings
//   - pinned width / height on a child
//   - row-reverse + column-reverse
// =============================================================================
#include <cd/ui/layout/Flex.hpp>
#include <gtest/gtest.h>

namespace ll = cd::ui::layout;

namespace
{
constexpr float kEps = 0.01F;
}  // namespace

// ---- Single child fills parent ---------------------------------------------

TEST(FlexLayout, SingleChildStretchesToParent)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.direction = ll::FlexDirection::kRow;
    auto root = t.create_node(root_s);

    ll::FlexStyle child_s;
    child_s.flex_grow = 1.0F;
    auto child = t.create_node(child_s);
    t.add_child(root, child);

    t.solve(root, 200.0F, 100.0F);
    const auto cr = t.layout(child);
    EXPECT_NEAR(cr.x, 0.0F, kEps);
    EXPECT_NEAR(cr.y, 0.0F, kEps);
    EXPECT_NEAR(cr.width,  200.0F, kEps);
    EXPECT_NEAR(cr.height, 100.0F, kEps);  // stretch (default align_items)
}

// ---- Three siblings, equal flex-grow ---------------------------------------

TEST(FlexLayout, ThreeEqualGrowChildrenInRow)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.flex_grow = 1.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    auto cc = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);
    t.add_child(root, cc);

    t.solve(root, 300.0F, 50.0F);
    EXPECT_NEAR(t.layout(a).width,  100.0F, kEps);
    EXPECT_NEAR(t.layout(b).width,  100.0F, kEps);
    EXPECT_NEAR(t.layout(cc).width, 100.0F, kEps);
    EXPECT_NEAR(t.layout(a).x,    0.0F, kEps);
    EXPECT_NEAR(t.layout(b).x,  100.0F, kEps);
    EXPECT_NEAR(t.layout(cc).x, 200.0F, kEps);
}

// ---- justify-content variants ---------------------------------------------

TEST(FlexLayout, JustifyCenterCentersChildren)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.justify = ll::JustifyContent::kCenter;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width = 50.0F;
    c.height = 50.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);

    // 200 - 50 - 50 = 100 free; center: 50 leading offset.
    t.solve(root, 200.0F, 100.0F);
    EXPECT_NEAR(t.layout(a).x, 50.0F, kEps);
    EXPECT_NEAR(t.layout(b).x, 100.0F, kEps);
}

TEST(FlexLayout, JustifySpaceBetween)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.justify = ll::JustifyContent::kSpaceBetween;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width = 40.0F;
    c.height = 40.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    auto cc = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);
    t.add_child(root, cc);

    // 240 - 3*40 = 120 free; 2 gaps of 60 between three items.
    t.solve(root, 240.0F, 100.0F);
    EXPECT_NEAR(t.layout(a).x,    0.0F, kEps);
    EXPECT_NEAR(t.layout(b).x,  100.0F, kEps);
    EXPECT_NEAR(t.layout(cc).x, 200.0F, kEps);
}

TEST(FlexLayout, JustifySpaceEvenly)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.justify = ll::JustifyContent::kSpaceEvenly;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width = 40.0F;
    c.height = 40.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    auto cc = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);
    t.add_child(root, cc);

    // 240 - 3*40 = 120; 4 even gaps of 30.
    t.solve(root, 240.0F, 100.0F);
    EXPECT_NEAR(t.layout(a).x,  30.0F, kEps);
    EXPECT_NEAR(t.layout(b).x, 100.0F, kEps);  // 30 + 40 + 30
    EXPECT_NEAR(t.layout(cc).x, 170.0F, kEps);  // 100 + 40 + 30
}

// ---- align-items variants -------------------------------------------------

TEST(FlexLayout, AlignCenterCentersOnCrossAxis)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.align_items = ll::AlignItems::kCenter;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width = 40.0F;
    c.height = 40.0F;
    auto a = t.create_node(c);
    t.add_child(root, a);

    t.solve(root, 100.0F, 100.0F);
    EXPECT_NEAR(t.layout(a).x, 0.0F, kEps);
    EXPECT_NEAR(t.layout(a).y, 30.0F, kEps);  // (100-40)/2
}

TEST(FlexLayout, AlignStretchExpandsCross)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.align_items = ll::AlignItems::kStretch;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.flex_grow = 1.0F;  // grow main
    // No fixed height -> stretch.
    auto a = t.create_node(c);
    t.add_child(root, a);

    t.solve(root, 100.0F, 60.0F);
    EXPECT_NEAR(t.layout(a).width,  100.0F, kEps);
    EXPECT_NEAR(t.layout(a).height,  60.0F, kEps);
}

// ---- Column direction ------------------------------------------------------

TEST(FlexLayout, ColumnStacksChildrenVertically)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.direction = ll::FlexDirection::kColumn;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.height = 40.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);

    t.solve(root, 100.0F, 200.0F);
    EXPECT_NEAR(t.layout(a).y,  0.0F, kEps);
    EXPECT_NEAR(t.layout(b).y, 40.0F, kEps);
    EXPECT_NEAR(t.layout(a).height, 40.0F, kEps);
    EXPECT_NEAR(t.layout(b).height, 40.0F, kEps);
    EXPECT_NEAR(t.layout(a).width,  100.0F, kEps);  // stretch on cross (X)
}

// ---- Padding offsets children inside the inner rect ----------------------

TEST(FlexLayout, PaddingOffsetsChildren)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.padding = ll::EdgeInsets { 10.0F, 5.0F, 15.0F, 20.0F };  // top right bottom left
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.flex_grow = 1.0F;
    auto a = t.create_node(c);
    t.add_child(root, a);

    t.solve(root, 200.0F, 100.0F);
    // Inner rect: width = 200 - 5 - 20 = 175, height = 100 - 10 - 15 = 75.
    // Child starts at (left=20, top=10) with size (175, 75).
    EXPECT_NEAR(t.layout(a).x, 20.0F, kEps);
    EXPECT_NEAR(t.layout(a).y, 10.0F, kEps);
    EXPECT_NEAR(t.layout(a).width,  175.0F, kEps);
    EXPECT_NEAR(t.layout(a).height,  75.0F, kEps);
}

// ---- Gap accumulates between siblings ------------------------------------

TEST(FlexLayout, GapMainPlacesSpaceBetweenSiblings)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.gap_main = 10.0F;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width  = 40.0F;
    c.height = 40.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    auto cc = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);
    t.add_child(root, cc);

    t.solve(root, 200.0F, 50.0F);
    EXPECT_NEAR(t.layout(a).x,  0.0F,  kEps);
    EXPECT_NEAR(t.layout(b).x,  50.0F, kEps);  // 40 + 10
    EXPECT_NEAR(t.layout(cc).x, 100.0F, kEps);  // 90 + 10
}

// ---- flex-shrink when content overflows ----------------------------------

TEST(FlexLayout, ShrinkProportionalToBase)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    auto root = t.create_node(root_s);

    ll::FlexStyle a_s;
    a_s.width = 100.0F;
    a_s.height = 50.0F;
    a_s.flex_shrink = 1.0F;
    auto a = t.create_node(a_s);
    ll::FlexStyle b_s = a_s;
    b_s.width = 200.0F;
    auto b = t.create_node(b_s);
    t.add_child(root, a);
    t.add_child(root, b);

    // Total natural width 300; available 240; overflow 60. Shrink shares:
    //   a_share = (1*100)/(1*100 + 1*200) = 1/3 -> a shrinks 20.
    //   b_share = 2/3                       -> b shrinks 40.
    t.solve(root, 240.0F, 50.0F);
    EXPECT_NEAR(t.layout(a).width,  80.0F, kEps);
    EXPECT_NEAR(t.layout(b).width, 160.0F, kEps);
}

// ---- min_width clamp -----------------------------------------------------

TEST(FlexLayout, MinWidthClampsShrink)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    auto root = t.create_node(root_s);

    ll::FlexStyle a_s;
    a_s.width = 100.0F;
    a_s.min_width = 90.0F;  // refuse to shrink below 90
    a_s.flex_shrink = 1.0F;
    auto a = t.create_node(a_s);
    t.add_child(root, a);

    t.solve(root, 50.0F, 50.0F);
    EXPECT_NEAR(t.layout(a).width, 90.0F, kEps);  // clamped to min
}

// ---- Row-reverse positions from right ------------------------------------

TEST(FlexLayout, RowReversePlacesChildrenFromRight)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.direction = ll::FlexDirection::kRowReverse;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width  = 40.0F;
    c.height = 40.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);

    t.solve(root, 200.0F, 50.0F);
    // First child a sits at right edge: x = 200 - 40 = 160; b at 120.
    EXPECT_NEAR(t.layout(a).x, 160.0F, kEps);
    EXPECT_NEAR(t.layout(b).x, 120.0F, kEps);
}

// ---- Nested tree --------------------------------------------------------

TEST(FlexLayout, NestedTreeRespectsParentInnerRect)
{
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.padding = ll::EdgeInsets { 10.0F, 10.0F, 10.0F, 10.0F };
    auto root = t.create_node(root_s);

    ll::FlexStyle inner_s;
    inner_s.flex_grow = 1.0F;
    auto inner = t.create_node(inner_s);
    t.add_child(root, inner);

    ll::FlexStyle leaf_s;
    leaf_s.width  = 30.0F;
    leaf_s.height = 30.0F;
    auto leaf = t.create_node(leaf_s);
    t.add_child(inner, leaf);

    t.solve(root, 200.0F, 200.0F);
    // root inner rect = 180x180 starting at (10,10). inner takes that.
    EXPECT_NEAR(t.layout(inner).x, 10.0F, kEps);
    EXPECT_NEAR(t.layout(inner).width, 180.0F, kEps);
    // leaf at (0,0) inside inner's local frame, projected to world: (10,10).
    EXPECT_NEAR(t.layout(leaf).x, 10.0F, kEps);
    EXPECT_NEAR(t.layout(leaf).y, 10.0F, kEps);
}

// ---- Edge / negative tests -----------------------------------------------

// Zero-size container: child with grow receives zero rect.
TEST(FlexLayout, ZeroSizeContainer_ChildGetsZeroRect)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.flex_grow = 1.0F;
    auto child = t.create_node(c);
    t.add_child(root, child);

    // Act
    t.solve(root, 0.0F, 0.0F);

    // Assert
    const auto r = t.layout(child);
    EXPECT_NEAR(r.width,  0.0F, kEps);
    EXPECT_NEAR(r.height, 0.0F, kEps);
}

// justify-content: flex-end packs children at the main-axis end.
TEST(FlexLayout, JustifyFlexEnd_PacksAtEnd)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.justify = ll::JustifyContent::kFlexEnd;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width  = 40.0F;
    c.height = 40.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);

    // Act
    // 200 available, 2×40 = 80 used, 120 free → a at 120, b at 160.
    t.solve(root, 200.0F, 50.0F);

    // Assert
    EXPECT_NEAR(t.layout(a).x, 120.0F, kEps);
    EXPECT_NEAR(t.layout(b).x, 160.0F, kEps);
}

// justify-content: space-around places half-gap on edges, full-gap between.
TEST(FlexLayout, JustifySpaceAround_CorrectGaps)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.justify = ll::JustifyContent::kSpaceAround;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width  = 40.0F;
    c.height = 40.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    auto cc = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);
    t.add_child(root, cc);

    // Act
    // 240 - 3×40 = 120 free; 3 equal slots → 40 each; half-slot (20) on each edge.
    t.solve(root, 240.0F, 50.0F);

    // Assert
    EXPECT_NEAR(t.layout(a).x,   20.0F, kEps);   // half-gap leading
    EXPECT_NEAR(t.layout(b).x,  100.0F, kEps);   // 20 + 40 + 40
    EXPECT_NEAR(t.layout(cc).x, 180.0F, kEps);   // 100 + 40 + 40
}

// align-items: flex-start places child at cross-axis start.
TEST(FlexLayout, AlignFlexStart_CrossAxisAtStart)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.align_items = ll::AlignItems::kFlexStart;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width  = 40.0F;
    c.height = 20.0F;
    auto a = t.create_node(c);
    t.add_child(root, a);

    // Act
    t.solve(root, 100.0F, 100.0F);

    // Assert: child y == 0, height == 20 (not stretched to 100).
    EXPECT_NEAR(t.layout(a).y,      0.0F, kEps);
    EXPECT_NEAR(t.layout(a).height, 20.0F, kEps);
}

// align-items: flex-end places child at cross-axis end.
TEST(FlexLayout, AlignFlexEnd_CrossAxisAtEnd)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.align_items = ll::AlignItems::kFlexEnd;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width  = 40.0F;
    c.height = 30.0F;
    auto a = t.create_node(c);
    t.add_child(root, a);

    // Act
    t.solve(root, 100.0F, 80.0F);

    // Assert: child y == 80 - 30 == 50.
    EXPECT_NEAR(t.layout(a).y,      50.0F, kEps);
    EXPECT_NEAR(t.layout(a).height, 30.0F, kEps);
}

// gap_main larger than available space: no overflow panic; children clamp to 0.
TEST(FlexLayout, GapExceedsAvailable_ChildrenDoNotOverflow)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.gap_main = 500.0F;  // enormous gap
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width  = 20.0F;
    c.height = 20.0F;
    c.flex_shrink = 1.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);

    // Act
    t.solve(root, 100.0F, 50.0F);

    // Assert: both children still have non-negative sizes.
    EXPECT_GE(t.layout(a).width, 0.0F);
    EXPECT_GE(t.layout(b).width, 0.0F);
}

// 3-level deep nesting: world rects accumulate correctly.
TEST(FlexLayout, DeeplyNested3Levels_WorldRectsAccumulate)
{
    // Arrange: root(pad=5) → mid(pad=5) → leaf(w=10, h=10)
    ll::FlexTree t;

    ll::FlexStyle root_s;
    root_s.padding = ll::EdgeInsets { 5.0F, 5.0F, 5.0F, 5.0F };
    auto root = t.create_node(root_s);

    ll::FlexStyle mid_s;
    mid_s.flex_grow = 1.0F;
    mid_s.padding = ll::EdgeInsets { 5.0F, 5.0F, 5.0F, 5.0F };
    auto mid = t.create_node(mid_s);
    t.add_child(root, mid);

    ll::FlexStyle leaf_s;
    leaf_s.width  = 10.0F;
    leaf_s.height = 10.0F;
    auto leaf = t.create_node(leaf_s);
    t.add_child(mid, leaf);

    // Act
    t.solve(root, 100.0F, 100.0F);

    // Assert: leaf world x = root.pad(5) + mid.pad(5) = 10; y = 10.
    EXPECT_NEAR(t.layout(leaf).x, 10.0F, kEps);
    EXPECT_NEAR(t.layout(leaf).y, 10.0F, kEps);
    EXPECT_NEAR(t.layout(leaf).width,  10.0F, kEps);
    EXPECT_NEAR(t.layout(leaf).height, 10.0F, kEps);
}

// Negative intrinsic values are treated as kAuto (< 0.0F sentinel).
TEST(FlexLayout, NegativeIntrinsicTreatedAsAuto)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.intrinsic_width  = -5.0F;   // negative → kAuto sentinel
    c.intrinsic_height = -5.0F;
    c.flex_grow = 1.0F;
    auto child = t.create_node(c);
    t.add_child(root, child);

    // Act — should not crash or produce nonsense.
    t.solve(root, 100.0F, 60.0F);

    // Assert: grow fills main; stretch fills cross.
    EXPECT_NEAR(t.layout(child).width,  100.0F, kEps);
    EXPECT_NEAR(t.layout(child).height,  60.0F, kEps);
}

// column-reverse stacks children bottom-to-top.
TEST(FlexLayout, ColumnReverse_StacksFromBottom)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.direction = ll::FlexDirection::kColumnReverse;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.height = 40.0F;
    auto a = t.create_node(c);
    auto b = t.create_node(c);
    t.add_child(root, a);
    t.add_child(root, b);

    // Act
    t.solve(root, 100.0F, 200.0F);

    // Assert: first child (a) placed at bottom (y = 160), b at y = 120.
    EXPECT_NEAR(t.layout(a).y, 160.0F, kEps);
    EXPECT_NEAR(t.layout(b).y, 120.0F, kEps);
}

// Shrink past effective min: child with min_width is clamped even when
// flex_shrink would push it below zero.
TEST(FlexLayout, ShrinkPastMin_ClampedAtMinWidth)
{
    // Arrange: two children in 60px; a(w=80,min=70,shrink=1) b(w=80,shrink=1).
    ll::FlexTree t;
    ll::FlexStyle root_s;
    auto root = t.create_node(root_s);

    ll::FlexStyle a_s;
    a_s.width      = 80.0F;
    a_s.height     = 20.0F;
    a_s.min_width  = 70.0F;
    a_s.flex_shrink = 1.0F;
    auto a = t.create_node(a_s);

    ll::FlexStyle b_s;
    b_s.width      = 80.0F;
    b_s.height     = 20.0F;
    b_s.flex_shrink = 1.0F;
    auto b = t.create_node(b_s);

    t.add_child(root, a);
    t.add_child(root, b);

    // Act
    // Available 60, need 160, overflow 100. Shrink weighted: both base=80.
    // Raw shrink each = 50. a would reach 30 but min_width=70 clamps it to 70.
    t.solve(root, 60.0F, 20.0F);

    // Assert: a must not go below min_width.
    EXPECT_GE(t.layout(a).width, 70.0F - kEps);
}

// kAbsolute child is placed by inset_left / inset_top, excluded from flow.
TEST(FlexLayout, AbsoluteChild_PlacedByInsets)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.padding = ll::EdgeInsets { 0.0F, 0.0F, 0.0F, 0.0F };
    auto root = t.create_node(root_s);

    // Flow child that takes all space.
    ll::FlexStyle flow_s;
    flow_s.flex_grow = 1.0F;
    auto flow = t.create_node(flow_s);
    t.add_child(root, flow);

    // Absolute child pinned 10px from left and 20px from top, 50×30.
    ll::FlexStyle abs_s;
    abs_s.position     = ll::PositionType::kAbsolute;
    abs_s.inset_left   = 10.0F;
    abs_s.inset_top    = 20.0F;
    abs_s.width        = 50.0F;
    abs_s.height       = 30.0F;
    auto abs_child = t.create_node(abs_s);
    t.add_child(root, abs_child);

    // Act
    t.solve(root, 200.0F, 100.0F);

    // Assert: flow child fills entire 200×100 (absolute is outside flow).
    EXPECT_NEAR(t.layout(flow).width,  200.0F, kEps);
    EXPECT_NEAR(t.layout(flow).height, 100.0F, kEps);

    // Absolute child sits at (10, 20) with size (50, 30).
    EXPECT_NEAR(t.layout(abs_child).x,      10.0F, kEps);
    EXPECT_NEAR(t.layout(abs_child).y,      20.0F, kEps);
    EXPECT_NEAR(t.layout(abs_child).width,  50.0F, kEps);
    EXPECT_NEAR(t.layout(abs_child).height, 30.0F, kEps);
}

// Margin offsets child from cross-axis edge (margin.top in row direction).
TEST(FlexLayout, MarginTop_OffsetsCrossPosition)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    root_s.align_items = ll::AlignItems::kFlexStart;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width         = 40.0F;
    c.height        = 20.0F;
    c.margin.top    = 15.0F;
    auto child = t.create_node(c);
    t.add_child(root, child);

    // Act
    t.solve(root, 100.0F, 100.0F);

    // Assert: y offset by margin.top (15).
    EXPECT_NEAR(t.layout(child).y, 15.0F, kEps);
}

// Margin on main axis (left) shifts child along row.
TEST(FlexLayout, MarginLeft_OffsetsMainPosition)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    c.width        = 40.0F;
    c.height       = 40.0F;
    c.margin.left  = 20.0F;
    auto child = t.create_node(c);
    t.add_child(root, child);

    // Act
    t.solve(root, 200.0F, 50.0F);

    // Assert: x == margin.left (20).
    EXPECT_NEAR(t.layout(child).x, 20.0F, kEps);
}

// Single-child flex-grow == 0 with no intrinsic: width stays at 0.
TEST(FlexLayout, SingleChildNoGrowNoIntrinsic_ZeroWidth)
{
    // Arrange
    ll::FlexTree t;
    ll::FlexStyle root_s;
    auto root = t.create_node(root_s);

    ll::FlexStyle c;
    // flex_grow = 0 (default), no width, no intrinsic.
    auto child = t.create_node(c);
    t.add_child(root, child);

    // Act
    t.solve(root, 100.0F, 50.0F);

    // Assert: child has zero main-axis size.
    EXPECT_NEAR(t.layout(child).width, 0.0F, kEps);
}
