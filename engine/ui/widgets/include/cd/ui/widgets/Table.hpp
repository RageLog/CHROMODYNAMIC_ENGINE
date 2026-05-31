// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/Table.hpp
//
// Phase 621 / M8 W3C — tabular data display widget in cd::ui::widgets.
//
// Design contract
// ---------------
// Table is a stateful, retained-mode widget that owns:
//   * A column descriptor list (header label, min_width, sortable flag).
//   * A cell-text store indexed by (row, col).
//   * A single-row selection index (optional, cleared when row_count shrinks
//     below the selection).
//   * A range selection (optional CellRange, representing a rectangular block).
//
// Public API (headless-safe — no GPU)
// ------------------------------------
//   set_columns(span<const Column>)     -- replace column definitions
//   set_row_count(size_t)               -- resize row count (clears orphaned cells)
//   set_cell_text(row, col, string)     -- set display text for one cell
//   draw(DrawBatcher, Theme, Rect)      -- emit quads (1 header + N row quads)
//   selected_row()  -> optional<size_t> -- current single-row selection
//   selection()     -> optional<CellRange> -- current block selection
//   simulate_click(x, y, Rect)         -- pointer hit-test + state transition
//                                          (for headless tests / tooling)
//
// Draw layout within `bounds`
// ---------------------------
//   [bounds.y .. bounds.y + kHeaderHeight]  header row (column labels)
//   [bounds.y + kHeaderHeight .. ...]        data rows of height kRowHeight each
//
// Each column occupies a proportional share of bounds.w, subject to
// Column::min_width. When total min_widths exceed bounds.w the columns
// overflow (no horizontal scroll in Phase 621 — deferred to Phase 2).
//
// simulate_click hit-test
// -----------------------
// Maps (x, y) into (row, col) using the same layout geometry as draw().
// A click on the header row is a no-op for row selection.
// A click on a data cell selects that row (single-row selection).
// Calling simulate_click twice on the same row does NOT deselect it
// (deselect requires explicit set_row_count or out-of-bounds click).
//
// Sortable flag
// -------------
// Column::sortable is stored as metadata only in Phase 621. The widget
// does NOT reorder rows internally; callers read sortable flags and
// re-supply sorted data via set_cell_text. This matches the DockSpace /
// ColorPicker philosophy: state machine + data owned by the widget, the
// rendering / ordering policy owned by the caller.
// =============================================================================
#pragma once

#include <cd/ui/widgets/Widgets.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cd::ui::renderer
{
class DrawBatcher;
}
namespace cd::ui::font
{
class Font;
}

namespace cd::ui::widgets
{

// ---- Column descriptor -------------------------------------------------------

/// Describes one table column.
struct Column
{
    std::string header    {};       ///< Header label text
    float       min_width { 60.0F }; ///< Minimum column width in pixels
    bool        sortable  { false };  ///< Column supports sort-click (metadata only)
};

// ---- CellRange ---------------------------------------------------------------

/// Axis-aligned rectangular range of cells selected by the user.
struct CellRange
{
    std::size_t row_start  { 0U };
    std::size_t row_count  { 0U };
    std::size_t col_start  { 0U };
    std::size_t col_count  { 0U };

    [[nodiscard]] bool contains(std::size_t row, std::size_t col) const noexcept
    {
        return row >= row_start && row < row_start + row_count
            && col >= col_start && col < col_start + col_count;
    }
};

// ---- Table -------------------------------------------------------------------

/// Retained-mode table widget. Owns column descriptors, cell-text storage,
/// and row/range selection state. Renderer-agnostic for headless testing.
class Table
{
public:
    static constexpr float kHeaderHeight { 24.0F };  ///< Header row height (px)
    static constexpr float kRowHeight    { 20.0F };  ///< Data row height (px)

    Table() = default;

    Table(const Table&)            = default;
    Table& operator=(const Table&) = default;
    Table(Table&&)                 = default;
    Table& operator=(Table&&)      = default;

    // ---- Column / row setup -------------------------------------------------

    /// Replace the column list. Clears any cell text that referred to columns
    /// beyond the new count.
    void set_columns(std::span<const Column> cols);

    /// Set total row count. Rows below the previous count lose their cell text.
    void set_row_count(std::size_t n);

    /// Store display text for one cell. Silently ignored when row >= row_count
    /// or col >= column_count().
    void set_cell_text(std::size_t row, std::size_t col, std::string text);

    // ---- Accessors ----------------------------------------------------------

    [[nodiscard]] std::size_t column_count() const noexcept
    {
        return columns_.size();
    }

    [[nodiscard]] std::size_t row_count() const noexcept
    {
        return row_count_;
    }

    [[nodiscard]] std::span<const Column> columns() const noexcept
    {
        return std::span<const Column>(columns_.data(), columns_.size());
    }

    /// Get cell text. Returns empty string_view when (row, col) is out of range
    /// or cell was never set.
    [[nodiscard]] std::string_view cell_text(std::size_t row, std::size_t col) const noexcept;

    // ---- Selection ----------------------------------------------------------

    /// Single-row selection (nullopt = nothing selected).
    [[nodiscard]] std::optional<std::size_t> selected_row() const noexcept
    {
        return selected_row_;
    }

    /// Block selection (nullopt = nothing selected).
    [[nodiscard]] std::optional<CellRange> selection() const noexcept
    {
        return selection_;
    }

    /// Clear both selection and block-selection.
    void clear_selection() noexcept;

    // ---- Input (headless / test) --------------------------------------------

    /// Map (x, y) inside `bounds` to a data cell and update row selection.
    /// Clicks on the header row are no-ops (header is not a selectable row).
    /// Out-of-bounds clicks (below last row) clear the selection.
    void simulate_click(float x, float y, const Rect& bounds);

    // ---- Rendering ----------------------------------------------------------

    /// Emit draw commands for the header row and all data rows inside `bounds`.
    /// The number of quads emitted equals: 1 (header bg) + row_count() (row bgs)
    /// + (1 per cell that has text) is NOT emitted — only layout quads.
    /// In Phase 621 cell text draw is stubbed (no font dependency on draw path).
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const Theme&                   theme,
              const Rect&                    bounds) const;

private:
    // ---- Internal layout helper ---------------------------------------------

    /// Compute column x-offsets and widths inside `bounds_w`. Returns a vector
    /// of (x_offset, width) pairs, one per column. Respects min_width; any
    /// remaining space is distributed evenly when total min_width < bounds_w.
    [[nodiscard]] std::vector<std::pair<float, float>>
    compute_col_layout_(float bounds_w) const noexcept;

    /// Stable key for the cell map: packed (row << 32 | col).
    [[nodiscard]] static std::uint64_t cell_key_(std::size_t row,
                                                 std::size_t col) noexcept
    {
        return (static_cast<std::uint64_t>(row) << 32U)
             |  static_cast<std::uint64_t>(col & 0xFFFFFFFFULL);
    }

    // ---- State --------------------------------------------------------------

    std::vector<Column>                        columns_      {};
    std::size_t                                row_count_    { 0U };
    std::unordered_map<std::uint64_t, std::string> cells_   {};

    std::optional<std::size_t>                 selected_row_ {};
    std::optional<CellRange>                   selection_    {};
};

}  // namespace cd::ui::widgets
