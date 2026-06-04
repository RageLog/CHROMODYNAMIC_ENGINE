// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_asset_pipeline_status/AssetPipelineStatus.hpp
//
// phase709 — cd::editor::panel::asset_pipeline_status (panel_asset_pipeline_status)
//
// Visualizes cd::asset::streamer_pool::StreamerPool state as four horizontal
// bar charts — one per streamer domain (scene / texture / audio / shader) —
// showing pending vs. completed counts.
//
// Colour coding:
//   accent_warning  — pending > 10 (pipeline is busy / potential bottleneck)
//   accent_success  — completed > pending (pipeline is draining or idle)
//   default accent  — otherwise (low pending, catching up)
//
// Layout (top → bottom):
//   Outer dark background + thin accent border.
//   Four horizontal stacked rows, each row consisting of:
//     Left label quad   (fixed width) — identifies the streamer domain.
//     Pending bar       (proportional to pending count, capped at kMaxBarCount).
//     Divider gap.
//     Completed bar     (proportional to completed count).
//     Right: counts are encoded into bar widths (no font rendering needed).
//
// API:
//   void set_pool(const cd::asset::streamer_pool::StreamerPool*)
//     — Attach (or detach with nullptr) the pool to observe.  Non-owning.
//
//   std::size_t total_pending() const
//     — Sum of all four pending counts from the last stats() snapshot.
//       Returns 0 when no pool is attached.
//
//   std::size_t total_completed() const
//     — Sum of all four completed counts.  Returns 0 when no pool is attached.
//
//   void draw(DrawBatcher&, const Theme&, const Rect& bounds) const
//     — Emit the four-row bar chart into `batcher` within `bounds`.
//       No-op for zero-sized bounds.  Calls pool->stats() internally.
//
// MOMENT: A dev watches the asset pipeline panel during scene-load — they see
//   which streamer is the bottleneck (sceneStreamer pending=50,
//   textureStreamer pending=200) and tune priorities before the next
//   PlayTest.  The panel requires zero terminal usage.
//
// Thread safety:
//   draw / total_pending / total_completed — read-only; safe to call from the
//     render thread provided the attached pool is not mutated concurrently.
//   set_pool — single-thread (caller must ensure no concurrent draw() calls).
// =============================================================================
#pragma once

#include <cd/asset/streamer_pool/StreamerPool.hpp>
#include <cd/core/Defines.hpp>

#include <cstddef>

// Forward-declare heavy UI types so this header stays lightweight.
namespace cd::ui::renderer
{
class DrawBatcher;
}
namespace cd::ui::widgets
{
struct Theme;
struct Rect;
}

namespace cd::editor::panel::asset_pipeline_status
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Maximum count value that fills a bar to 100 % width.
/// Counts above this are clamped; the bar still reads as "full".
inline constexpr std::uint32_t kMaxBarCount = 200U;

/// Threshold above which the pending bar switches to accent_warning colour.
inline constexpr std::uint32_t kWarningThreshold = 10U;

// ---------------------------------------------------------------------------
// AssetPipelineStatus
// ---------------------------------------------------------------------------

class AssetPipelineStatus
{
public:
    // Construction -----------------------------------------------------------

    AssetPipelineStatus()  noexcept = default;
    ~AssetPipelineStatus() noexcept = default;

    // Non-copyable; moveable.
    AssetPipelineStatus(const AssetPipelineStatus&)            = delete;
    AssetPipelineStatus& operator=(const AssetPipelineStatus&) = delete;
    AssetPipelineStatus(AssetPipelineStatus&&)                 = default;
    AssetPipelineStatus& operator=(AssetPipelineStatus&&)      = default;

    // Pool attachment --------------------------------------------------------

    /// Attach a StreamerPool to observe.  Pass nullptr to detach.
    /// Non-owning: the pool must outlive this panel.
    void set_pool(const cd::asset::streamer_pool::StreamerPool* pool) noexcept;

    // Observation ------------------------------------------------------------

    /// Sum of scene + texture + audio + shader pending counts.
    /// Calls pool->stats() each invocation; returns 0 when no pool attached.
    [[nodiscard]] std::size_t total_pending()   const noexcept;

    /// Sum of scene + texture + audio + shader completed counts.
    /// Returns 0 when no pool is attached.
    [[nodiscard]] std::size_t total_completed() const noexcept;

    // Draw API ---------------------------------------------------------------

    /// Emit four horizontal bar-chart rows into `batcher` within `bounds`.
    ///
    /// Each row visualizes one streamer domain (scene / texture / audio /
    /// shader):
    ///   Pending bar  — proportional to pending count / kMaxBarCount.
    ///   Completed bar — proportional to completed count / kMaxBarCount.
    ///
    /// Colour rules per row:
    ///   pending bar  > kWarningThreshold  → accent_warning  (yellow)
    ///   completed   > pending             → accent_success   (green)
    ///   otherwise                         → accent           (blue)
    ///
    /// No-op when bounds.w <= 0 or bounds.h <= 0.
    /// Does NOT call begin_frame() / end_frame() on the batcher.
    void draw(cd::ui::renderer::DrawBatcher&  batcher,
              const cd::ui::widgets::Theme&   theme,
              const cd::ui::widgets::Rect&    bounds) const;

private:
    // Non-owning pointer to the observed pool.
    const cd::asset::streamer_pool::StreamerPool* pool_ { nullptr };

    // ---- Layout constants --------------------------------------------------

    static constexpr float kBorderW    =  1.0F;  ///< Outer border thickness (px).
    static constexpr float kPadding    =  4.0F;  ///< Inner padding (px).
    static constexpr float kRowGap     =  3.0F;  ///< Vertical gap between rows.
    static constexpr float kBarH       = 10.0F;  ///< Height of each bar pair (px).
    static constexpr float kBarGap     =  2.0F;  ///< Gap between pending/completed bars.
    static constexpr float kLabelW     = 60.0F;  ///< Width of the domain label quad.
};

}  // namespace cd::editor::panel::asset_pipeline_status
