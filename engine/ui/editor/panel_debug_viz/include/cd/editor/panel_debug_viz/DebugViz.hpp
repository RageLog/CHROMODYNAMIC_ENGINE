// =============================================================================
// CHROMODYNAMIC — cd/editor/panel_debug_viz/DebugViz.hpp
//
// phase684 — cd::editor::debug_viz  (panel_debug_viz library)
//
// Debug Visualization overlay: a compact ~200x150 px thumbnail that renders
// one of three G-buffer debug views in the top-right corner of the editor.
//
// Supported kinds (VizKind enum):
//   kDepth       — grayscale depth thumbnail: closer = brighter, farther = darker.
//                  Sprint-1 placeholder: vertical gradient from white (top) to
//                  black (bottom). Real G-buffer depth sampling in Sprint-2 when
//                  cd::render::framegraph exposes depth textures cleanly.
//   kNormal      — world-normal RGB thumbnail: normal.xyz * 0.5 + 0.5.
//                  Sprint-1: RGB gradient quad (R left→right, G top→bottom, B centre).
//   kAlphaBucket — per-pixel render-bucket colour:
//                    kOpaque     → green  (88, 200,  88)
//                    kAlphaMask  → yellow (220, 200,  60)
//                    kAlphaBlend → red    (220,  60,  60)
//                  Sprint-1: three vertical colour bands (opaque | mask | blend).
//                  Sprint-2: real G-buffer bucket classification pass.
//
// Toggle API:
//   set_visible(bool)    — enable / disable drawing.
//   is_visible() const   — query current toggle state.
//
// Draw:
//   draw(DrawBatcher&, Rect bounds) const
//     — Emits quads into `batcher` within `bounds`. Draws a dark background,
//       a 1 px accent border, a colour-coded header strip (4 px), then the
//       kind-specific gradient tiles. No-op when !is_visible().
//
// MOMENT: A render dev hits a curtain-bleed-through bug, toggles 'Alpha Bucket'
// viz, sees the curtain correctly classified as red (kAlphaBlend), confirms the
// framegraph order is right, and narrows the bug elsewhere. Three thumbnails
// in the top-right corner; one glance reveals depth distribution, world-normal
// coherence, and alpha classification — without leaving the editor.
//
// Lifetime contract:
//   DebugVizOverlay is default-constructible and owns no external resources.
//   All state is value-type (bool + enum). Thread-safe for read, single-thread
//   for write (call set_visible from the same thread as draw).
// =============================================================================
#pragma once

#include <cd/ui/renderer/DrawBatcher.hpp>
#include <cd/ui/widgets/Widgets.hpp>

#include <cstdint>

namespace cd::editor::debug_viz
{

// ---------------------------------------------------------------------------
// VizKind — which G-buffer channel this overlay visualises.
// ---------------------------------------------------------------------------
enum class VizKind : std::uint8_t
{
    kDepth       = 0,  ///< Grayscale depth (closer = brighter).
    kNormal      = 1,  ///< World normals as RGB: xyz * 0.5 + 0.5.
    kAlphaBucket = 2,  ///< Render-bucket colour: green/yellow/red.
};

// ---------------------------------------------------------------------------
// DebugVizOverlay
// ---------------------------------------------------------------------------
class DebugVizOverlay
{
public:
    /// Construct with a fixed kind. Starts visible (toggle ON by default).
    explicit DebugVizOverlay(VizKind kind) noexcept;

    // ---- Toggle API ---------------------------------------------------------

    /// Show or hide this overlay.
    void set_visible(bool visible) noexcept;

    /// Returns true when the overlay will draw.
    [[nodiscard]] bool is_visible() const noexcept;

    /// Returns the kind this overlay visualises.
    [[nodiscard]] VizKind kind() const noexcept;

    // ---- DrawBatcher path ---------------------------------------------------

    /// Emit draw commands into `batcher` within `bounds`.
    ///
    /// No-op when !is_visible() or bounds is invalid (w <= 0 or h <= 0).
    ///
    /// Draws (in order):
    ///   1. Dark background fill.
    ///   2. 1 px accent-coloured border.
    ///   3. 4 px kind-coloured header strip (identifies the viz at a glance).
    ///   4. Kind-specific placeholder gradient tiles (Sprint-1).
    ///
    /// Thread-safety: call from the render thread only.
    void draw(cd::ui::renderer::DrawBatcher& batcher,
              const cd::ui::widgets::Rect&   bounds) const;

private:
    VizKind kind_;
    bool    visible_ { true };

    // Internal helpers --------------------------------------------------------

    void draw_depth(cd::ui::renderer::DrawBatcher& batcher,
                    const cd::ui::widgets::Rect&   content) const;

    void draw_normal(cd::ui::renderer::DrawBatcher& batcher,
                     const cd::ui::widgets::Rect&   content) const;

    void draw_alpha_bucket(cd::ui::renderer::DrawBatcher& batcher,
                           const cd::ui::widgets::Rect&   content) const;

    // Header strip height in pixels.
    static constexpr float kHeaderH  = 4.0F;
    // Border width in pixels.
    static constexpr float kBorderW  = 1.0F;
    // Accent colour per kind: depth=blue, normal=cyan, alpha-bucket=orange.
    [[nodiscard]] static cd::ui::renderer::Color header_color(VizKind k) noexcept;
};

}  // namespace cd::editor::debug_viz
