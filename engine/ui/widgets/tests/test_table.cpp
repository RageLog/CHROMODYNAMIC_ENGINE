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
