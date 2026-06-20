// =============================================================================
// CHROMODYNAMIC — cd::ui::widgets::Table tests
//
// Phase 621 / M8 W3C. Seven test cases covering:
//   1. Default construction — empty columns + zero rows.
//   2. set_columns + set_row_count round-trip.
//   3. set_cell_text round-trip (get back what was stored).
//   4. set_cell_text out-of-range is silently ignored.
//   5. draw() emits header quad + one quad per row (vertex count check).
//   6. simulate_click selects correct row.
//   7. Sortable column flag is stored and reported accurately.
//
// All tests are CPU-only — no GPU resources touched, no font loaded.
// =============================================================================
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Table.hpp>

#include <gtest/gtest.h>

#include <array>
#include <string>

namespace w = cd::ui::widgets;
namespace r = cd::ui::renderer;

namespace
{

constexpr w::Rect kBounds { 0.0F, 0.0F, 400.0F, 300.0F };

w::Theme make_theme() noexcept { return w::Theme {}; }

std::array<w::Column, 3> three_cols()
{
    return {
        w::Column { "Name",  100.0F, false },
        w::Column { "Value",  80.0F, true  },
        w::Column { "Notes", 120.0F, false },
    };
}

}  // namespace

// =============================================================================
// Case 1: Default construction — zero columns, zero rows, no selection.
// =============================================================================
TEST(UiWidgetsTable, DefaultConstruction)
{
    const w::Table t;
    EXPECT_EQ(t.column_count(), 0U);
    EXPECT_EQ(t.row_count(),    0U);
    EXPECT_FALSE(t.selected_row().has_value());
    EXPECT_FALSE(t.selection().has_value());
}

// =============================================================================
// Case 2: set_columns + set_row_count round-trip.
// =============================================================================
TEST(UiWidgetsTable, SetColumnsAndRowCount)
{
    w::Table t;
    const auto cols = three_cols();
    t.set_columns(cols);
    EXPECT_EQ(t.column_count(), 3U);

    t.set_row_count(10U);
    EXPECT_EQ(t.row_count(), 10U);

    // Column headers preserved.
    EXPECT_EQ(t.columns()[0].header, "Name");
    EXPECT_EQ(t.columns()[1].header, "Value");
    EXPECT_EQ(t.columns()[2].header, "Notes");
}

// =============================================================================
// Case 3: set_cell_text round-trip.
// =============================================================================
TEST(UiWidgetsTable, CellTextRoundTrip)
{
    w::Table t;
    const auto cols = three_cols();
    t.set_columns(cols);
    t.set_row_count(5U);

    t.set_cell_text(0U, 0U, "Alpha");
    t.set_cell_text(4U, 2U, "Omega");

    EXPECT_EQ(t.cell_text(0U, 0U), "Alpha");
    EXPECT_EQ(t.cell_text(4U, 2U), "Omega");
    // Unset cell returns empty.
    EXPECT_EQ(t.cell_text(1U, 1U), "");
}

// =============================================================================
// Case 4: set_cell_text out-of-range is silently ignored.
// =============================================================================
TEST(UiWidgetsTable, CellTextOutOfRangeIgnored)
{
    w::Table t;
    const auto cols = three_cols();
    t.set_columns(cols);
    t.set_row_count(3U);

    // Row out of range.
    t.set_cell_text(99U, 0U, "bad");
    EXPECT_EQ(t.cell_text(99U, 0U), "");

    // Col out of range.
    t.set_cell_text(0U, 99U, "bad");
    EXPECT_EQ(t.cell_text(0U, 99U), "");
}

// =============================================================================
// Case 5: draw() emits expected number of quads.
//
// Expected quad count for N columns and R rows:
//   1  (header background)
//   + (N-1)  (header column dividers — one per interior boundary)
//   + R  (row backgrounds)
//   + R * (N-1)  (row column dividers)
//   = 1 + (N-1) + R + R*(N-1)
//   = 1 + (N-1)*(R+1) + R
//
// Vertex count = quad_count * 4 (each quad = 4 vertices in the batcher).
// =============================================================================
TEST(UiWidgetsTable, DrawEmitsCorrectQuadCount)
{
    w::Table t;
    const auto cols = three_cols();  // 3 columns
    t.set_columns(cols);
    const std::size_t R = 4U;
    t.set_row_count(R);

    r::DrawBatcher batcher;
    batcher.begin_frame();
    t.draw(batcher, make_theme(), kBounds);

    constexpr std::size_t N = 3U;
    // header bg + (N-1) dividers + R row bgs + R*(N-1) row dividers
    const std::size_t expected_quads = 1U + (N - 1U) + R + R * (N - 1U);
    // Each quad = 4 vertices.
    EXPECT_EQ(batcher.vertex_count(), expected_quads * 4U);
}

// =============================================================================
// Case 6: simulate_click selects the correct row.
// =============================================================================
TEST(UiWidgetsTable, SimulateClickSelectsCorrectRow)
{
    w::Table t;
    const auto cols = three_cols();
    t.set_columns(cols);
    t.set_row_count(5U);

    // Click on row 2 (0-indexed). Row 0 starts at bounds.y + kHeaderHeight.
    // Row 2 starts at bounds.y + kHeaderHeight + 2 * kRowHeight.
    const float click_y = kBounds.y
                        + w::Table::kHeaderHeight
                        + 2.0F * w::Table::kRowHeight
                        + 2.0F;  // small offset inside the row

    t.simulate_click(kBounds.x + 50.0F, click_y, kBounds);

    ASSERT_TRUE(t.selected_row().has_value());
    EXPECT_EQ(*t.selected_row(), 2U);

    // Click below all rows clears selection.
    const float below_y = kBounds.y
                        + w::Table::kHeaderHeight
                        + static_cast<float>(t.row_count()) * w::Table::kRowHeight
                        + 10.0F;
    t.simulate_click(kBounds.x + 50.0F, below_y, kBounds);
    EXPECT_FALSE(t.selected_row().has_value());
}

// =============================================================================
// Case 7: Sortable column flag is stored and reported correctly.
// =============================================================================
TEST(UiWidgetsTable, SortableFlagRespected)
{
    w::Table t;
    const auto cols = three_cols();
    // cols[0].sortable = false, cols[1].sortable = true, cols[2].sortable = false
    t.set_columns(cols);

    EXPECT_FALSE(t.columns()[0].sortable);
    EXPECT_TRUE (t.columns()[1].sortable);
    EXPECT_FALSE(t.columns()[2].sortable);
}

// =============================================================================
// Case 8: clear_selection resets both row and block selection.
// =============================================================================
TEST(UiWidgetsTable, ClearSelectionResetsBothFields)
{
    w::Table t;
    const auto cols = three_cols();
    t.set_columns(cols);
    t.set_row_count(5U);

    // Establish a row selection via simulate_click.
    const float click_y = kBounds.y + w::Table::kHeaderHeight + 1.0F;
    t.simulate_click(kBounds.x + 50.0F, click_y, kBounds);

    ASSERT_TRUE(t.selected_row().has_value());
    ASSERT_TRUE(t.selection().has_value());

    t.clear_selection();

    EXPECT_FALSE(t.selected_row().has_value());
    EXPECT_FALSE(t.selection().has_value());
}

// =============================================================================
// Case 9: CellRange::contains respects both row and column bounds.
// =============================================================================
TEST(UiWidgetsTable, CellRangeContainsBothAxes)
{
    const w::CellRange cr { 2U, 3U, 1U, 2U };  // rows 2-4, cols 1-2

    EXPECT_TRUE(cr.contains(2U, 1U));   // top-left corner
    EXPECT_TRUE(cr.contains(4U, 2U));   // bottom-right corner
    EXPECT_TRUE(cr.contains(3U, 1U));   // interior

    EXPECT_FALSE(cr.contains(1U, 1U));  // row before start
    EXPECT_FALSE(cr.contains(5U, 1U));  // row after end
    EXPECT_FALSE(cr.contains(3U, 0U));  // col before start
    EXPECT_FALSE(cr.contains(3U, 3U));  // col after end
}

// =============================================================================
// Case 10: set_columns clears cells that belonged to orphaned columns.
// =============================================================================
TEST(UiWidgetsTable, SetColumnsEvictsOrphanedCells)
{
    w::Table t;
    const auto cols = three_cols();
    t.set_columns(cols);     // 3 columns
    t.set_row_count(3U);

    t.set_cell_text(0U, 2U, "col2-data");
    ASSERT_EQ(t.cell_text(0U, 2U), "col2-data");

    // Shrink to 2 columns — col 2 cell must be evicted.
    const std::array<w::Column, 2> short_cols {{
        w::Column { "A", 60.0F, false },
        w::Column { "B", 60.0F, false },
    }};
    t.set_columns(short_cols);

    EXPECT_EQ(t.cell_text(0U, 2U), "");
}

// =============================================================================
// Case 11: simulate_click on the header row is a no-op (no row selection change).
// =============================================================================
TEST(UiWidgetsTable, SimulateClickOnHeaderIsNoOp)
{
    w::Table t;
    const auto cols = three_cols();
    t.set_columns(cols);
    t.set_row_count(5U);

    // Click in the header band.
    const float header_mid_y = kBounds.y + w::Table::kHeaderHeight * 0.5F;
    t.simulate_click(kBounds.x + 50.0F, header_mid_y, kBounds);

    EXPECT_FALSE(t.selected_row().has_value());
}

// =============================================================================
// Case 12: set_row_count clears selection when selected row falls beyond new count.
// =============================================================================
TEST(UiWidgetsTable, SetRowCountClearsOutOfRangeSelection)
{
    w::Table t;
    const auto cols = three_cols();
    t.set_columns(cols);
    t.set_row_count(10U);

    // Select row 8.
    const float click_y = kBounds.y + w::Table::kHeaderHeight
                        + 8.0F * w::Table::kRowHeight + 2.0F;
    t.simulate_click(kBounds.x + 50.0F, click_y, kBounds);
    ASSERT_TRUE(t.selected_row().has_value());
    EXPECT_EQ(*t.selected_row(), 8U);

    // Shrink row count to 5 — selection at row 8 must be cleared.
    t.set_row_count(5U);
    EXPECT_FALSE(t.selected_row().has_value());
    EXPECT_FALSE(t.selection().has_value());
}

// =============================================================================
// Case 13: Column layout distributes extra space evenly when sum < bounds_w.
// =============================================================================
TEST(UiWidgetsTable, ColumnLayoutDistributesExtraSpaceEvenly)
{
    // Two equal min_width columns in a bounds wider than their sum.
    // Each should get (total - 2*min_w) / 2 extra space.
    const std::array<w::Column, 2> two_cols {{
        w::Column { "X", 60.0F, false },
        w::Column { "Y", 60.0F, false },
    }};
    const w::Rect wide_bounds { 0.0F, 0.0F, 200.0F, 200.0F };

    w::Table t;
    t.set_columns(two_cols);
    t.set_row_count(1U);

    // Click in the right half (>= 100 px) — must select col 1.
    const float click_x = 150.0F;
    const float click_y = wide_bounds.y + w::Table::kHeaderHeight + 2.0F;
    t.simulate_click(click_x, click_y, wide_bounds);

    ASSERT_TRUE(t.selection().has_value());
    EXPECT_EQ(t.selection()->col_start, 1U);
}
