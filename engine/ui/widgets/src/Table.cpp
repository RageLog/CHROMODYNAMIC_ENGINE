// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/Table.cpp
//
// Phase 621 / M8 W3C implementation of cd::ui::widgets::Table.
//
// Column layout algorithm
// -----------------------
// Each column claims its min_width first. If the sum of min_widths is less
// than bounds_w, the leftover space is divided evenly across all columns
// (ensuring every column gets at least its min_width).  When the sum of
// min_widths exceeds bounds_w each column is assigned exactly min_width and
// the table overflows (scrolling is deferred to a later phase).
//
// Cell-text storage
// -----------------
// Sparse: an unordered_map keyed by a packed (row<<32 | col) uint64 avoids
// allocating a full row*col grid for sparse tables. Eviction on set_row_count
// or set_columns is done with a single erase_if pass.
//
// Draw
// ----
// Emits solid-colour quads only (no font / glyph path in Phase 621).
//   * 1 header background quad (theme.surface)
//   * N row background quads alternating theme.background / theme.surface
//   * 1 highlight quad for the selected row (theme.accent, 50 % alpha)
// Column dividers are drawn as 1-pixel wide quads (theme.text_dim).
// =============================================================================
#include <cd/ui/widgets/Table.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <cstdint>
#include <numeric>

namespace cd::ui::widgets
{

// ---- helpers -----------------------------------------------------------------

namespace
{

[[nodiscard]] cd::ui::renderer::Color to_rcolor(Color c) noexcept
{
    return cd::ui::renderer::Color { c.r, c.g, c.b, c.a };
}

[[nodiscard]] cd::ui::renderer::Color with_alpha(Color c, std::uint8_t a) noexcept
{
    return cd::ui::renderer::Color { c.r, c.g, c.b, a };
}

}  // namespace

// ---- set_columns ------------------------------------------------------------

void Table::set_columns(std::span<const Column> cols)
{
    columns_.assign(cols.begin(), cols.end());

    const std::size_t ncols = columns_.size();
    // Evict cells that refer to columns beyond the new count.
    std::erase_if(cells_, [ncols](const auto& kv) {
        const std::size_t col = static_cast<std::size_t>(kv.first & 0xFFFFFFFFULL);
        return col >= ncols;
    });

    // Clear selection if it now extends beyond column bounds.
    if (selection_)
    {
        const CellRange& sel = *selection_;
        if (sel.col_start >= ncols || sel.col_start + sel.col_count > ncols)
        {
            selection_.reset();
        }
    }
}

// ---- set_row_count ----------------------------------------------------------

void Table::set_row_count(std::size_t n)
{
    row_count_ = n;

    // Evict cells for rows that no longer exist.
    std::erase_if(cells_, [n](const auto& kv) {
        const std::size_t row = static_cast<std::size_t>(kv.first >> 32U);
        return row >= n;
    });

    // Clamp / clear row selection.
    if (selected_row_ && *selected_row_ >= n)
    {
        selected_row_.reset();
    }

    // Clamp / clear block selection.
    if (selection_)
    {
        const CellRange& sel = *selection_;
        if (sel.row_start >= n || sel.row_start + sel.row_count > n)
        {
            selection_.reset();
        }
    }
}

// ---- set_cell_text ----------------------------------------------------------

void Table::set_cell_text(std::size_t row, std::size_t col, std::string text)
{
    if (row >= row_count_ || col >= columns_.size())
    {
        return;  // silent out-of-range guard
    }
    cells_[cell_key_(row, col)] = std::move(text);
}

// ---- cell_text --------------------------------------------------------------

[[nodiscard]] std::string_view Table::cell_text(std::size_t row,
                                                 std::size_t col) const noexcept
{
    const auto it = cells_.find(cell_key_(row, col));
    if (it == cells_.end())
    {
        return {};
    }
    return it->second;
}

// ---- clear_selection --------------------------------------------------------

void Table::clear_selection() noexcept
{
    selected_row_.reset();
    selection_.reset();
}

// ---- simulate_click ---------------------------------------------------------

void Table::simulate_click(float x, float y, const Rect& bounds)
{
    // Must be inside the bounds horizontally at minimum.
    if (x < bounds.x || x >= bounds.x + bounds.w)
    {
        clear_selection();
        return;
    }

    // Header zone — not selectable.
    const float header_bottom = bounds.y + kHeaderHeight;
    if (y < bounds.y || y < header_bottom)
    {
        return;
    }

    // Data row zone.
    const float  data_y     = y - header_bottom;
    const float  row_height = kRowHeight;
    const auto   row_idx    = static_cast<std::size_t>(data_y / row_height);

    if (row_idx >= row_count_)
    {
        // Click below all rows — clear selection.
        clear_selection();
        return;
    }

    selected_row_ = row_idx;

    // Also compute col index and update block selection to single-cell range.
    const auto col_layout = compute_col_layout_(bounds.w);
    const float rel_x     = x - bounds.x;

    std::size_t col_idx = 0U;
    for (std::size_t c = 0U; c < col_layout.size(); ++c)
    {
        const float cx = col_layout[c].first;
        const float cw = col_layout[c].second;
        if (rel_x >= cx && rel_x < cx + cw)
        {
            col_idx = c;
            break;
        }
    }

    selection_ = CellRange { row_idx, 1U, col_idx, 1U };
}

// ---- compute_col_layout_ ----------------------------------------------------

[[nodiscard]] std::vector<std::pair<float, float>>
Table::compute_col_layout_(float bounds_w) const noexcept
{
    const std::size_t ncols = columns_.size();
    if (ncols == 0U)
    {
        return {};
    }

    // Sum of minimum widths.
    float total_min = 0.0F;
    for (const Column& col : columns_)
    {
        total_min += col.min_width;
    }

    const float extra       = bounds_w > total_min ? bounds_w - total_min : 0.0F;
    const float extra_each  = extra / static_cast<float>(ncols);

    std::vector<std::pair<float, float>> layout;
    layout.reserve(ncols);

    float cx = 0.0F;
    for (const Column& col : columns_)
    {
        const float cw = col.min_width + extra_each;
        layout.emplace_back(cx, cw);
        cx += cw;
    }

    return layout;
}

// ---- draw -------------------------------------------------------------------

void Table::draw(cd::ui::renderer::DrawBatcher& batcher,
                 const Theme&                   theme,
                 const Rect&                    bounds) const
{
    if (!bounds.is_valid())
    {
        return;
    }

    const auto col_layout = compute_col_layout_(bounds.w);

    // --- Header row ----------------------------------------------------------
    batcher.quad(bounds.x, bounds.y,
                 bounds.w, kHeaderHeight,
                 to_rcolor(theme.surface));

    // Column dividers in the header.
    constexpr float kDividerW = 1.0F;
    for (std::size_t c = 1U; c < col_layout.size(); ++c)
    {
        const float div_x = bounds.x + col_layout[c].first;
        batcher.quad(div_x, bounds.y,
                     kDividerW, kHeaderHeight,
                     to_rcolor(theme.text_dim));
    }

    // --- Data rows -----------------------------------------------------------
    const float data_top = bounds.y + kHeaderHeight;

    for (std::size_t r = 0U; r < row_count_; ++r)
    {
        const float ry = data_top + static_cast<float>(r) * kRowHeight;

        // Alternate row background.
        const Color row_bg = (r % 2U == 0U) ? theme.background : theme.surface;
        batcher.quad(bounds.x, ry,
                     bounds.w, kRowHeight,
                     to_rcolor(row_bg));

        // Highlight selected row.
        if (selected_row_ && *selected_row_ == r)
        {
            batcher.quad(bounds.x, ry,
                         bounds.w, kRowHeight,
                         with_alpha(theme.accent, 128U));
        }

        // Column dividers.
        for (std::size_t c = 1U; c < col_layout.size(); ++c)
        {
            const float div_x = bounds.x + col_layout[c].first;
            batcher.quad(div_x, ry,
                         kDividerW, kRowHeight,
                         to_rcolor(theme.text_dim));
        }
    }
}

}  // namespace cd::ui::widgets
