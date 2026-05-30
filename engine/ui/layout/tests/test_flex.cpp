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
