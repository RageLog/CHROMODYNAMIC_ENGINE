// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_asset_drop_target/src/AssetDropTarget.cpp
//
// phase594 — cd::editor::panel::asset_drop_target  implementation
// =============================================================================
#include <cd/editor/panel_asset_drop_target/AssetDropTarget.hpp>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>

namespace cd::editor::panel::asset_drop_target
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Extract the file extension (including the leading dot) from a path.
/// Returns an empty string_view if none is found.
static std::string_view extension_of(std::string_view path) noexcept
{
    const auto dot_pos = path.rfind('.');
    if (dot_pos == std::string_view::npos)
        return {};
    // Ensure the dot is not from a directory component.
    const auto slash_pos = path.find_last_of("/\\");
    if (slash_pos != std::string_view::npos && dot_pos < slash_pos)
        return {};
    return path.substr(dot_pos);
}

// ---------------------------------------------------------------------------
// Filter API
// ---------------------------------------------------------------------------

void AssetDropTarget::set_accepted_extensions(std::span<const std::string> exts)
{
    accepted_exts_.assign(exts.begin(), exts.end());
}

std::size_t AssetDropTarget::extension_count() const noexcept
{
    return accepted_exts_.size();
}

// ---------------------------------------------------------------------------
// Drop API
// ---------------------------------------------------------------------------

bool AssetDropTarget::accepts(const std::string& path) const
{
    if (accepted_exts_.empty())
        return true;

    const std::string_view ext = extension_of(path);
    return std::ranges::any_of(accepted_exts_,
                               [ext](const std::string& accepted) {
                                   return std::string_view { accepted } == ext;
                               });
}

bool AssetDropTarget::consume_dropped_path(std::string& out_path)
{
    if (!pending_drop_.has_value())
        return false;

    out_path = std::move(*pending_drop_);
    pending_drop_.reset();
    return true;
}

void AssetDropTarget::simulate_drop(std::string path)
{
    if (!accepts(path))
        return;
    pending_drop_ = std::move(path);
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void AssetDropTarget::draw(cd::ui::renderer::DrawBatcher& batcher,
                           const cd::ui::widgets::Theme&  theme,
                           const cd::ui::widgets::Rect&   bounds) const
{
    // --- Background fill ---
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color {
                     theme.surface.r,
                     theme.surface.g,
                     theme.surface.b,
                     theme.surface.a });

    if (!bounds.is_valid())
        return;

    constexpr float kBorderW  = 2.0F;   // dash strip thickness
    constexpr float kDashLen  = 12.0F;  // length of each dash segment
    constexpr float kGapLen   = 6.0F;   // gap between dash segments
    constexpr float kPad      = 8.0F;   // inset from the bounds edge

    const float inner_x = bounds.x + kPad;
    const float inner_y = bounds.y + kPad;
    const float inner_w = bounds.w - 2.0F * kPad;
    const float inner_h = bounds.h - 2.0F * kPad;

    if (inner_w <= 0.0F || inner_h <= 0.0F)
        return;

    // Border colour: accent with reduced alpha for the "dashed" aesthetic.
    const cd::ui::renderer::Color border_col {
        theme.accent.r,
        theme.accent.g,
        theme.accent.b,
        160U };

    // Helper: emit dashes along the horizontal or vertical axis.
    auto emit_h_dashes = [&](float sx, float sy, float length)
    {
        float cursor = sx;
        const float end_x = sx + length;
        bool on = true;
        while (cursor < end_x)
        {
            const float seg = on ? kDashLen : kGapLen;
            const float draw_len = std::min(seg, end_x - cursor);
            if (on)
                batcher.quad(cursor, sy, draw_len, kBorderW, border_col);
            cursor += draw_len;
            on = !on;
        }
    };

    auto emit_v_dashes = [&](float sx, float sy, float length)
    {
        float cursor = sy;
        const float end_y = sy + length;
        bool on = true;
        while (cursor < end_y)
        {
            const float seg = on ? kDashLen : kGapLen;
            const float draw_len = std::min(seg, end_y - cursor);
            if (on)
                batcher.quad(sx, cursor, kBorderW, draw_len, border_col);
            cursor += draw_len;
            on = !on;
        }
    };

    // Top edge.
    emit_h_dashes(inner_x, inner_y, inner_w);
    // Bottom edge.
    emit_h_dashes(inner_x, inner_y + inner_h - kBorderW, inner_w);
    // Left edge (excluding corners already drawn).
    emit_v_dashes(inner_x, inner_y + kBorderW, inner_h - 2.0F * kBorderW);
    // Right edge.
    emit_v_dashes(inner_x + inner_w - kBorderW, inner_y + kBorderW,
                  inner_h - 2.0F * kBorderW);

    // Centre label placeholder — a dim horizontal strip indicating the zone.
    constexpr float kLabelH   = 4.0F;
    constexpr float kLabelPad = 24.0F;
    const float label_w       = inner_w - 2.0F * kLabelPad;
    if (label_w > 0.0F)
    {
        const float label_x = inner_x + kLabelPad;
        const float label_y = inner_y + (inner_h - kLabelH) * 0.5F;
        batcher.quad(label_x, label_y, label_w, kLabelH,
                     cd::ui::renderer::Color {
                         theme.text_dim.r,
                         theme.text_dim.g,
                         theme.text_dim.b,
                         100U });
    }
}

}  // namespace cd::editor::panel::asset_drop_target
