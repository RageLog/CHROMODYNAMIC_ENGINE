// =============================================================================
// CHROMODYNAMIC — samples/ui/hello_widgets_table/main.cpp
//
// Phase 635 — M9 W2 headless console proof of cd::ui::widgets::Table.
//
// What this sample proves
// -----------------------
//   * Table can be constructed and configured without a GPU or platform window.
//   * set_columns / set_row_count / set_cell_text work headlessly.
//   * set_cell_text on the diagonal (row == col, 4 diagonal cells).
//   * simulate_click selects a row and updates selected_row().
//   * draw() emits quads into a cd::ui::renderer::DrawBatcher;
//     command_count() reflects the emitted draw commands.
//
// Output (stdout):
//   selected_row: 3
//   draw_cmd_count: <N>
//
// Exit code: 0 on success, 1 on assertion failure.
// =============================================================================

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Table.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>

int main()
{
    // ---- 1. Build a 4-column, 10-row Table ----------------------------------

    static constexpr std::size_t kNumCols = 4U;
    static constexpr std::size_t kNumRows = 10U;

    const std::array<cd::ui::widgets::Column, kNumCols> cols {{
        { "Col A", 80.0F, false },
        { "Col B", 80.0F, false },
        { "Col C", 80.0F, false },
        { "Col D", 80.0F, false },
    }};

    cd::ui::widgets::Table table;
    table.set_columns(std::span<const cd::ui::widgets::Column>(cols.data(), cols.size()));
    table.set_row_count(kNumRows);

    // ---- 2. Populate diagonal cells -----------------------------------------
    //
    // 4 diagonal cells: (0,0), (1,1), (2,2), (3,3).
    for (std::size_t i = 0U; i < kNumCols; ++i)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "diag[%zu]", i);
        table.set_cell_text(i, i, buf);
    }

    // Sanity: diagonal cells must be retrievable.
    for (std::size_t i = 0U; i < kNumCols; ++i)
    {
        if (table.cell_text(i, i).empty())
        {
            std::fprintf(stderr, "FAIL: diagonal cell (%zu,%zu) is empty\n", i, i);
            return EXIT_FAILURE;
        }
    }

    // ---- 3. Define bounds and simulate_click on row 3 ----------------------
    //
    // Row 3 occupies:
    //   y_start = bounds.y + Table::kHeaderHeight + 3 * Table::kRowHeight
    //           = 0 + 24 + 60 = 84
    //   y_end   = y_start + Table::kRowHeight = 104
    // A click at (x=200, y=89) falls squarely inside row 3.

    const cd::ui::widgets::Rect bounds { 0.0F, 0.0F, 400.0F, 300.0F };

    const float click_x = 200.0F;
    const float click_y = bounds.y
                        + cd::ui::widgets::Table::kHeaderHeight
                        + 3.0F * cd::ui::widgets::Table::kRowHeight
                        + 5.0F;  // +5 px into row 3

    table.simulate_click(click_x, click_y, bounds);

    // ---- 4. Verify selection ------------------------------------------------

    if (!table.selected_row().has_value())
    {
        std::fprintf(stderr, "FAIL: no row selected after simulate_click\n");
        return EXIT_FAILURE;
    }

    const std::size_t sel = *table.selected_row();
    if (sel != 3U)
    {
        std::fprintf(stderr, "FAIL: expected selected_row==3, got %zu\n", sel);
        return EXIT_FAILURE;
    }

    // ---- 5. Draw into a DrawBatcher and report command count ----------------

    cd::ui::renderer::DrawBatcher batcher;
    batcher.begin_frame();

    const cd::ui::widgets::Theme theme {};  // default theme
    table.draw(batcher, theme, bounds);

    const std::size_t cmd_count = batcher.command_count();

    // The batcher must have emitted at least one command (header + rows).
    if (cmd_count == 0U)
    {
        std::fprintf(stderr, "FAIL: DrawBatcher command_count is 0 after draw\n");
        return EXIT_FAILURE;
    }

    // ---- 6. Print results ---------------------------------------------------

    std::printf("selected_row: %zu\n", sel);
    std::printf("draw_cmd_count: %zu\n", cmd_count);

    return EXIT_SUCCESS;
}
