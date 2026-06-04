// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_debug_viz/DebugViz.hpp
//
// phase696 — cd::editor::debug_viz  (panel_debug_viz library)
//            Refines phase684 Sprint-1 placeholders with:
//              • Per-overlay F-key hotkey visibility toggles.
//              • 60-frame sparkline ring buffers fed from cpu_marker /
//                gpu_marker / frame_graph_timeline sample data.
//              • Sparkline segments rendered as 1 px quads; segments that
//                exceed the 16.6 ms (60 fps) budget are drawn in the theme's
//                accent_warning colour.
//              • Alpha-bucket viz reads live DrawBatcher quad counts per
//                RenderBucket (kOpaque / kAlphaMask / kAlphaBlend) and
//                draws proportional colour bars instead of equal-width bands.
//
// phase735 — Sprint-2: real G-buffer texture overlays.
//            DebugVizOverlay now carries a live cd::rhi::TextureHandle.
//            When the bound texture is valid the overlay emits a
//            DrawBatcher::textured_quad covering the content area
//            (the lower 32 bits of the handle's raw value are forwarded
//            as the texture_slot — same convention used by
//            cd::editor::panel::viewport::Viewport).
//            When the texture handle is null, the overlay falls back to
//            the Sprint-1 gradient / bucket-bar placeholder so headless
//            and pre-render-thread frames are still legible.
//            Three kind-specific setters (set_depth_texture /
//            set_normal_texture / set_albedo_texture) route to the
//            matching VizKind only; calls against a non-matching kind
//            are silently ignored.
//
// Supported kinds (VizKind enum):
//   kDepth       — grayscale depth thumbnail (placeholder gradient). Hotkey F4.
//   kNormal      — world-normal RGB thumbnail. Hotkey F5.
//   kAlphaBucket — per-bucket quad-count bars / G-buffer albedo. Hotkey F6.
//
// Sparkline API (kDepth / kNormal feeds frame-timing ms per stage):
//   push_frame_sample(float ms)
//     — Append one ms value to the 60-slot ring. Oldest entry is evicted.
//       Called once per frame from the engine loop.
//
// Alpha-bucket API:
//   set_bucket_counts(std::uint32_t opaque, std::uint32_t mask,
//                     std::uint32_t blend)
//     — Update per-frame quad counts for the three render buckets.
//       Called once per frame after DrawBatcher::end_frame().
//
// Toggle API:
//   set_visible(bool)    — enable / disable drawing.
//   toggle()             — flip visibility; returns new state.
//   is_visible() const   — query current toggle state.
//
// Draw:
//   draw(DrawBatcher&, Rect bounds, Theme& theme) const
//     — Emits quads into `batcher` within `bounds`. Draws a dark background,
//       a 1 px accent border, a 4 px kind-coloured header strip, then:
//         kDepth / kNormal  → placeholder gradient + sparkline overlay.
//         kAlphaBucket      → proportional colour bars from set_bucket_counts.
//       No-op when !is_visible().
//
// MOMENT: A render dev pressing F4-F6 in rapid succession sees depth/normal/
// bucket viz toggle in real-time as they navigate the scene — debugging
// becomes visual + hotkey-driven, not console-log-driven.
//
// Lifetime contract:
//   DebugVizOverlay is value-moveable and owns its ring buffer state as a
//   plain array (no heap). Thread-safe for read; single-thread for write.
// =============================================================================
#pragma once

#include <cd/rhi/Handles.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <array>
#include <cstdint>

namespace cd::editor::debug_viz
{

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Number of frame-timing samples retained for sparkline rendering.
inline constexpr std::size_t kSparklineCapacity = 60U;

/// Target frame budget in milliseconds (60 fps). Samples exceeding this
/// threshold are rendered with accent_warning colour in the sparkline.
inline constexpr float kBudgetMs = 16.6F;

// ---------------------------------------------------------------------------
// VizKind — which G-buffer channel this overlay visualises.
// ---------------------------------------------------------------------------
enum class VizKind : std::uint8_t
{
    kDepth       = 0,  ///< Grayscale depth (closer = brighter). Hotkey F4.
    kNormal      = 1,  ///< World normals as RGB: xyz * 0.5 + 0.5. Hotkey F5.
    kAlphaBucket = 2,  ///< Render-bucket quad counts. Hotkey F6.
};

// ---------------------------------------------------------------------------
// BucketCounts — per-frame quad counts for each RenderBucket tier.
// ---------------------------------------------------------------------------
struct BucketCounts
{
    std::uint32_t opaque { 0U };     ///< kOpaque   quads (green bar).
    std::uint32_t mask   { 0U };     ///< kAlphaMask quads (yellow bar).
    std::uint32_t blend  { 0U };     ///< kAlphaBlend quads (red bar).

    /// Total quad count across all buckets. Returns 0 when all are zero.
    [[nodiscard]] std::uint32_t total() const noexcept
    {
        return opaque + mask + blend;
    }
};

// ---------------------------------------------------------------------------
// DebugVizOverlay
// ---------------------------------------------------------------------------
class DebugVizOverlay
{
public:
    /// Construct with a fixed kind. Starts visible (toggle ON by default).
    explicit DebugVizOverlay(VizKind kind) noexcept;

    // ---- Hotkey / Toggle API ------------------------------------------------

    /// Show or hide this overlay.
    void set_visible(bool visible) noexcept;

    /// Flip visibility. Returns the new state. Designed for hotkey dispatch:
    ///   overlay_depth_.toggle();  // F4 pressed
    [[nodiscard]] bool toggle() noexcept;

    /// Returns true when the overlay will draw.
    [[nodiscard]] bool is_visible() const noexcept;

    /// Returns the kind this overlay visualises.
    [[nodiscard]] VizKind kind() const noexcept;

    // ---- Sample Feed API (called once per frame) ----------------------------

    /// Push one frame-timing sample (in milliseconds) into the 60-slot
    /// sparkline ring.  The oldest entry is silently evicted when full.
    ///
    /// Applicable to kDepth and kNormal overlays (feeds CPU/GPU stage timings
    /// from cd::profile::cpu_marker_overlay / gpu_marker / frame_graph_timeline
    /// collector results).  Alpha-bucket overlays show this sparkline too.
    void push_frame_sample(float ms) noexcept;

    /// Set the live per-bucket quad counts emitted by the DrawBatcher this
    /// frame.  Applicable to kAlphaBucket overlays; ignored for others.
    void set_bucket_counts(std::uint32_t opaque,
                           std::uint32_t mask,
                           std::uint32_t blend) noexcept;

    /// Returns the current bucket counts.
    [[nodiscard]] const BucketCounts& bucket_counts() const noexcept;

    /// Returns the sample ring (oldest-first, up to kSparklineCapacity entries).
    /// The valid range is [0, sample_count()).
    [[nodiscard]] std::size_t sample_count() const noexcept;
    [[nodiscard]] float       sample_at(std::size_t i) const noexcept;

    // ---- G-buffer Texture API (phase735, Sprint-2) --------------------------

    /// Bind the live cd::rhi::TextureHandle this overlay should sample at
    /// draw() time. Pass a default-constructed (null) handle to clear the
    /// binding and fall back to the Sprint-1 gradient / bucket placeholder.
    ///
    /// The texture is kind-agnostic at this level; the kind-specific
    /// convenience setters below only forward when the kinds match so
    /// caller code can fire all three setters every frame and stay
    /// expressive.
    void set_texture(cd::rhi::TextureHandle handle) noexcept;

    /// Convenience: assign the depth G-buffer texture. No-op when the
    /// overlay's kind is not kDepth (calls against the wrong overlay are
    /// silently ignored so apps/editor can broadcast all three setters).
    void set_depth_texture(cd::rhi::TextureHandle handle) noexcept;

    /// Convenience: assign the world-normal G-buffer texture. No-op when
    /// the overlay's kind is not kNormal.
    void set_normal_texture(cd::rhi::TextureHandle handle) noexcept;

    /// Convenience: assign the albedo (base-colour) G-buffer texture.
    /// Routes to kAlphaBucket overlays — when a valid albedo texture is
    /// bound the overlay displays the sampled albedo instead of the
    /// per-bucket quad-count bars. No-op for other kinds.
    void set_albedo_texture(cd::rhi::TextureHandle handle) noexcept;

    /// Returns the currently bound texture (null when none was set).
    [[nodiscard]] cd::rhi::TextureHandle current_texture() const noexcept;

    // ---- DrawBatcher path ---------------------------------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// No-op when !is_visible() or bounds is invalid (w <= 0 or h <= 0).
    ///
    /// Draws (in order):
    ///   1. Dark background fill.
    ///   2. 1 px accent-coloured border.
    ///   3. 4 px kind-coloured header strip (identifies the viz at a glance).
    ///   4. Kind-specific content tiles.
    ///   5. 60-frame sparkline overlay (1 px quads; accent_warning when overbudget).
    ///
    /// Thread-safety: call from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Rect&   bounds,
              const cd::ui::widgets::Theme&  theme) const;

private:
    VizKind      kind_;
    bool         visible_ { true };

    // Sparkline ring buffer -------------------------------------------------
    std::array<float, kSparklineCapacity> samples_ {};
    std::size_t  sample_head_  { 0U };   ///< Next write position (circular).
    std::size_t  sample_count_ { 0U };   ///< Valid entries in ring (0..kSparklineCapacity).

    // Alpha-bucket state ----------------------------------------------------
    BucketCounts bucket_counts_ {};

    // G-buffer texture binding (phase735) ----------------------------------
    cd::rhi::TextureHandle texture_ {};   ///< Live G-buffer texture; null = placeholder.

    // Internal helpers -------------------------------------------------------

    void draw_depth(cd::ui::renderer::DrawBatcher& batcher,
                    const cd::ui::widgets::Rect&   content) const;

    void draw_normal(cd::ui::renderer::DrawBatcher& batcher,
                     const cd::ui::widgets::Rect&   content) const;

    void draw_alpha_bucket(cd::ui::renderer::DrawBatcher& batcher,
                           const cd::ui::widgets::Rect&   content) const;

    /// Draw a 60-frame sparkline in the bottom strip of `content`.
    /// Segments exceeding kBudgetMs are rendered in theme.accent_warning.
    void draw_sparkline(cd::ui::renderer::DrawBatcher& batcher,
                        const cd::ui::widgets::Rect&   content,
                        const cd::ui::widgets::Theme&  theme) const;

    // Header strip height in pixels.
    static constexpr float kHeaderH  = 4.0F;
    // Border width in pixels.
    static constexpr float kBorderW  = 1.0F;
    // Sparkline strip height at the bottom of content area.
    static constexpr float kSparkH   = 24.0F;
    // Accent colour per kind: depth=blue, normal=cyan, alpha-bucket=orange.
    [[nodiscard]] static cd::ui::renderer::Color header_color(VizKind k) noexcept;
};

}  // namespace cd::editor::debug_viz
