// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_asset_pipeline_status/src/AssetPipelineStatus.cpp
//
// phase709 — cd::editor::panel::asset_pipeline_status::AssetPipelineStatus
//
// Layout strategy
// ---------------
// The panel is divided into:
//   1. A dark outer background quad + a thin accent border (4 quads).
//   2. Four streamer rows (scene / texture / audio / shader), stacked
//      top-to-bottom inside the padded inner rect.
//
// Each row consists of (left → right):
//   a. Label quad       — kLabelW wide; domain-tinted background quad.
//   b. Pending bar      — proportional to pending / kMaxBarCount; colour is
//                         accent_warning when pending > kWarningThreshold,
//                         else theme.accent.
//   c. Divider gap      — kBarGap blank space.
//   d. Completed bar    — proportional to completed / kMaxBarCount; colour is
//                         accent_success when completed > pending, else accent.
//
// Bar width is computed from the available width after the label quad:
//   usable_w     = inner_w - kLabelW
//   half_usable  = (usable_w - kBarGap) / 2.0F
//   pending_bar_w   = clamp(pending  / kMaxBarCount) * half_usable
//   completed_bar_w = clamp(completed / kMaxBarCount) * half_usable
//
// All draw calls use DrawBatcher::quad() only (no RHI, no ImGui).
// =============================================================================
#include <cd/editor/panel_asset_pipeline_status/AssetPipelineStatus.hpp>

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace cd::editor::panel::asset_pipeline_status
{

// ===========================================================================
// Pool attachment
// ===========================================================================

void AssetPipelineStatus::set_pool(
    const cd::asset::streamer_pool::StreamerPool* pool) noexcept
{
    pool_ = pool;
}

// ===========================================================================
// Observation
// ===========================================================================

std::size_t AssetPipelineStatus::total_pending() const noexcept
{
    if (pool_ == nullptr) { return 0U; }
    const auto s = pool_->stats();
    return static_cast<std::size_t>(s.scene_pending)
         + static_cast<std::size_t>(s.texture_pending)
         + static_cast<std::size_t>(s.audio_pending)
         + static_cast<std::size_t>(s.shader_pending);
}

std::size_t AssetPipelineStatus::total_completed() const noexcept
{
    if (pool_ == nullptr) { return 0U; }
    const auto s = pool_->stats();
    return static_cast<std::size_t>(s.scene_completed)
         + static_cast<std::size_t>(s.texture_completed)
         + static_cast<std::size_t>(s.audio_completed)
         + static_cast<std::size_t>(s.shader_completed);
}

// ===========================================================================
// Draw
// ===========================================================================

void AssetPipelineStatus::draw(cd::ui::renderer::DrawBatcher& batcher,
                               const cd::ui::widgets::Theme&  theme,
                               const cd::ui::widgets::Rect&   bounds) const
{
    if (bounds.w <= 0.0F || bounds.h <= 0.0F) { return; }

    // ---- Outer dark background + accent border ----------------------------
    const cd::ui::renderer::Color bg { 14U, 14U, 18U, 240U };
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h, bg);

    // Border uses theme accent (unpack u8 fields to renderer::Color).
    const cd::ui::renderer::Color border {
        theme.accent.r,
        theme.accent.g,
        theme.accent.b,
        160U
    };
    // top / bottom / left / right
    batcher.quad(bounds.x,                        bounds.y,                        bounds.w, kBorderW,  border);
    batcher.quad(bounds.x,                        bounds.y + bounds.h - kBorderW,  bounds.w, kBorderW,  border);
    batcher.quad(bounds.x,                        bounds.y,                        kBorderW, bounds.h,  border);
    batcher.quad(bounds.x + bounds.w - kBorderW,  bounds.y,                        kBorderW, bounds.h,  border);

    // ---- Inner content rect -----------------------------------------------
    const float inner_x = bounds.x + kBorderW + kPadding;
    const float inner_y = bounds.y + kBorderW + kPadding;
    const float inner_w = bounds.w - 2.0F * (kBorderW + kPadding);
    const float inner_h = bounds.h - 2.0F * (kBorderW + kPadding);

    if (inner_w <= kLabelW + kBarGap || inner_h <= 0.0F) { return; }

    // ---- Fetch stats (or use zeroes when no pool attached) ----------------
    cd::asset::streamer_pool::PoolStats s {};
    if (pool_ != nullptr)
    {
        s = pool_->stats();
    }

    // Per-row data: { pending, completed } pairs for [scene, texture, audio, shader].
    struct RowData
    {
        std::uint32_t pending;
        std::uint32_t completed;
    };

    const std::array<RowData, 4> rows {{
        { s.scene_pending,   s.scene_completed   },
        { s.texture_pending, s.texture_completed },
        { s.audio_pending,   s.audio_completed   },
        { s.shader_pending,  s.shader_completed  },
    }};

    // Usable bar area width (right of the label).
    const float bar_area_w = inner_w - kLabelW;
    const float half_bar_w = (bar_area_w - kBarGap) * 0.5F;
    if (half_bar_w <= 0.0F) { return; }

    // Colour constants.
    const cd::ui::renderer::Color label_bg { 24U, 28U, 38U, 220U };
    const cd::ui::renderer::Color bar_bg   { 20U, 22U, 30U, 200U };

    // Theme-derived accent colours — unpack widgets::Color -> renderer::Color.
    const cd::ui::renderer::Color col_accent {
        theme.accent.r,
        theme.accent.g,
        theme.accent.b,
        theme.accent.a
    };
    const cd::ui::renderer::Color col_warning {
        theme.accent_warning.r,
        theme.accent_warning.g,
        theme.accent_warning.b,
        theme.accent_warning.a
    };
    const cd::ui::renderer::Color col_success {
        theme.accent_success.r,
        theme.accent_success.g,
        theme.accent_success.b,
        theme.accent_success.a
    };

    // Row-domain tints for label quads (distinguishable without text).
    const std::array<cd::ui::renderer::Color, 4> label_tints {{
        {  60U,  80U, 140U, 200U },   // scene   — blue-purple
        {  60U, 120U, 180U, 200U },   // texture — lighter blue
        {  80U, 140U,  80U, 200U },   // audio   — muted green
        { 120U,  80U, 140U, 200U },   // shader  — purple
    }};

    float cursor_y = inner_y;

    for (std::size_t i = 0U; i < rows.size(); ++i)
    {
        if (cursor_y + kBarH > inner_y + inner_h) { break; }

        const RowData& row = rows[i];
        const float    row_x = inner_x;
        const float    row_y = cursor_y;

        // ---- Label quad ----------------------------------------------------
        batcher.quad(row_x, row_y, kLabelW, kBarH, label_bg);
        batcher.quad(row_x, row_y, kLabelW, kBarH, label_tints[i]);

        // ---- Bar background ------------------------------------------------
        const float bars_x = row_x + kLabelW;
        batcher.quad(bars_x, row_y, bar_area_w, kBarH, bar_bg);

        // ---- Pending bar ---------------------------------------------------
        const float pending_norm = std::clamp(
            static_cast<float>(row.pending) / static_cast<float>(kMaxBarCount),
            0.0F, 1.0F);
        const float pending_bar_w = std::max(pending_norm * half_bar_w, 1.0F);

        const cd::ui::renderer::Color& pending_color =
            (row.pending > kWarningThreshold) ? col_warning : col_accent;

        batcher.quad(bars_x, row_y, pending_bar_w, kBarH, pending_color);

        // ---- Completed bar -------------------------------------------------
        const float completed_x = bars_x + half_bar_w + kBarGap;
        const float completed_norm = std::clamp(
            static_cast<float>(row.completed) / static_cast<float>(kMaxBarCount),
            0.0F, 1.0F);
        const float completed_bar_w = std::max(completed_norm * half_bar_w, 1.0F);

        const cd::ui::renderer::Color& completed_color =
            (row.completed > row.pending) ? col_success : col_accent;

        batcher.quad(completed_x, row_y, completed_bar_w, kBarH, completed_color);

        cursor_y += kBarH + kRowGap;
    }
}

}  // namespace cd::editor::panel::asset_pipeline_status
