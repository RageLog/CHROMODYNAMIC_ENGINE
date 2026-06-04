// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_debug_viz/src/DebugViz.cpp
//
// phase684 — cd::editor::debug_viz::DebugVizOverlay implementation.
//
// Sprint-1 placeholder rendering:
//   kDepth       — 8 horizontal bands top→bottom: white → black (grayscale ramp).
//   kNormal      — 3 vertical RGB bands: R (left), G (centre), B (right) +
//                  a blended green overlay to mimic the xyz * 0.5 + 0.5 remap.
//   kAlphaBucket — 3 equal vertical bands: green | yellow | red, labelled by
//                  render bucket. Thin separator lines (1 px dark) between
//                  bands to make the classification boundary visible.
//
// Sprint-2 (future): replace placeholder tiles with real G-buffer texture
// samples once cd::render::framegraph exposes the depth / normal / bucket
// textures as bindable handles in the editor overlay pipeline.
// =============================================================================
#include <cd/editor/panel_debug_viz/DebugViz.hpp>

#include <algorithm>
#include <cstdint>

namespace cd::editor::debug_viz
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

DebugVizOverlay::DebugVizOverlay(VizKind kind) noexcept
    : kind_(kind)
{
}

// ---------------------------------------------------------------------------
// Toggle API
// ---------------------------------------------------------------------------

void DebugVizOverlay::set_visible(bool visible) noexcept
{
    visible_ = visible;
}

bool DebugVizOverlay::is_visible() const noexcept
{
    return visible_;
}

VizKind DebugVizOverlay::kind() const noexcept
{
    return kind_;
}

// ---------------------------------------------------------------------------
// Header colour per kind
// ---------------------------------------------------------------------------

cd::ui::renderer::Color DebugVizOverlay::header_color(VizKind k) noexcept
{
    switch (k)
    {
        case VizKind::kDepth:
            // Blue — depth is the "cold" channel.
            return cd::ui::renderer::Color { 80U, 120U, 220U, 220U };
        case VizKind::kNormal:
            // Cyan — world-normals are often visualised in the teal / cyan family.
            return cd::ui::renderer::Color { 60U, 200U, 200U, 220U };
        case VizKind::kAlphaBucket:
            // Orange — a warm "bucket classifier" identity colour.
            return cd::ui::renderer::Color { 220U, 140U, 50U, 220U };
    }
    return cd::ui::renderer::Color { 120U, 120U, 120U, 200U };
}

// ---------------------------------------------------------------------------
// DrawBatcher path
// ---------------------------------------------------------------------------

void DebugVizOverlay::draw(cd::ui::renderer::DrawBatcher& batcher,
                           const cd::ui::widgets::Rect&   bounds) const
{
    if (!visible_) { return; }
    if (bounds.w <= 0.0F || bounds.h <= 0.0F) { return; }

    // ---- 1. Dark background -------------------------------------------------
    batcher.quad(bounds.x, bounds.y, bounds.w, bounds.h,
                 cd::ui::renderer::Color { 18U, 18U, 22U, 230U });

    // ---- 2. 1 px accent border ----------------------------------------------
    const auto bc = header_color(kind_);
    // Top edge.
    batcher.quad(bounds.x, bounds.y, bounds.w, kBorderW, bc);
    // Bottom edge.
    batcher.quad(bounds.x, bounds.y + bounds.h - kBorderW, bounds.w, kBorderW, bc);
    // Left edge.
    batcher.quad(bounds.x, bounds.y, kBorderW, bounds.h, bc);
    // Right edge.
    batcher.quad(bounds.x + bounds.w - kBorderW, bounds.y, kBorderW, bounds.h, bc);

    // ---- 3. 4 px kind-coloured header strip ---------------------------------
    batcher.quad(bounds.x + kBorderW,
                 bounds.y + kBorderW,
                 bounds.w - 2.0F * kBorderW,
                 kHeaderH,
                 bc);

    // ---- 4. Kind-specific placeholder tiles ---------------------------------
    // Content rect: inside border + below header.
    const cd::ui::widgets::Rect content {
        bounds.x + kBorderW,
        bounds.y + kBorderW + kHeaderH,
        bounds.w - 2.0F * kBorderW,
        bounds.h - 2.0F * kBorderW - kHeaderH,
    };

    if (content.w <= 0.0F || content.h <= 0.0F) { return; }

    switch (kind_)
    {
        case VizKind::kDepth:       draw_depth(batcher, content);        break;
        case VizKind::kNormal:      draw_normal(batcher, content);       break;
        case VizKind::kAlphaBucket: draw_alpha_bucket(batcher, content); break;
    }
}

// ---------------------------------------------------------------------------
// kDepth — 8 horizontal grayscale bands: white (top) → black (bottom)
// ---------------------------------------------------------------------------
void DebugVizOverlay::draw_depth(cd::ui::renderer::DrawBatcher& batcher,
                                 const cd::ui::widgets::Rect&   content) const
{
    static constexpr int kBands = 8;
    const float band_h = content.h / static_cast<float>(kBands);

    for (int i = 0; i < kBands; ++i)
    {
        // Value 1.0 at top (i=0), 0.0 at bottom (i=kBands-1).
        const float norm  = 1.0F - static_cast<float>(i) / static_cast<float>(kBands - 1);
        const auto  grey  = static_cast<std::uint8_t>(std::clamp(norm, 0.0F, 1.0F) * 255.0F);
        const float band_y = content.y + static_cast<float>(i) * band_h;
        batcher.quad(content.x, band_y, content.w, band_h,
                     cd::ui::renderer::Color { grey, grey, grey, 230U });
    }
}

// ---------------------------------------------------------------------------
// kNormal — RGB normal viz: normal.xyz * 0.5 + 0.5 remapping placeholder.
//
// Sprint-1 three-band approximation:
//   left  third  — R channel dominant  (high R, mid G, mid B)
//   centre third — G channel dominant  (mid R, high G, mid B)
//   right  third — B channel dominant  (mid R, mid G, high B)
// Each band gets a 128 midpoint baseline + ~100 channel lift to mimic the
// xyz * 0.5 + 0.5 remap visually without a real normal map.
// ---------------------------------------------------------------------------
void DebugVizOverlay::draw_normal(cd::ui::renderer::DrawBatcher& batcher,
                                  const cd::ui::widgets::Rect&   content) const
{
    const float third_w = content.w / 3.0F;

    // Left third — R dominant (facing +X ≈ orange/red in normal-map space).
    batcher.quad(content.x, content.y, third_w, content.h,
                 cd::ui::renderer::Color { 230U, 128U, 128U, 220U });

    // Centre third — G dominant (facing +Y ≈ green in normal-map space).
    batcher.quad(content.x + third_w, content.y, third_w, content.h,
                 cd::ui::renderer::Color { 128U, 230U, 128U, 220U });

    // Right third — B dominant (facing +Z ≈ blue in normal-map space;
    // the classic "facing forward" teal-blue of screen-space normals).
    batcher.quad(content.x + 2.0F * third_w, content.y, third_w, content.h,
                 cd::ui::renderer::Color { 128U, 200U, 230U, 220U });

    // Thin vertical separators to make the band structure legible.
    const cd::ui::renderer::Color sep { 18U, 18U, 22U, 200U };
    batcher.quad(content.x + third_w - 0.5F, content.y, 1.0F, content.h, sep);
    batcher.quad(content.x + 2.0F * third_w - 0.5F, content.y, 1.0F, content.h, sep);
}

// ---------------------------------------------------------------------------
// kAlphaBucket — three equal vertical bands:
//   kOpaque     → green  ( 88, 200,  88)
//   kAlphaMask  → yellow (220, 200,  60)
//   kAlphaBlend → red    (220,  60,  60)
//
// Sprint-1 the bands are equal-width; Sprint-2 they will be proportional to
// the pixel count of each bucket from the G-buffer classification pass.
// ---------------------------------------------------------------------------
void DebugVizOverlay::draw_alpha_bucket(cd::ui::renderer::DrawBatcher& batcher,
                                        const cd::ui::widgets::Rect&   content) const
{
    const float third_w = content.w / 3.0F;

    // kOpaque — green.
    batcher.quad(content.x, content.y, third_w, content.h,
                 cd::ui::renderer::Color { 88U, 200U, 88U, 220U });

    // kAlphaMask — yellow.
    batcher.quad(content.x + third_w, content.y, third_w, content.h,
                 cd::ui::renderer::Color { 220U, 200U, 60U, 220U });

    // kAlphaBlend — red.
    batcher.quad(content.x + 2.0F * third_w, content.y, third_w, content.h,
                 cd::ui::renderer::Color { 220U, 60U, 60U, 220U });

    // Thin vertical separators (dark) between bucket bands.
    const cd::ui::renderer::Color sep { 18U, 18U, 22U, 200U };
    batcher.quad(content.x + third_w - 0.5F, content.y, 1.0F, content.h, sep);
    batcher.quad(content.x + 2.0F * third_w - 0.5F, content.y, 1.0F, content.h, sep);
}

}  // namespace cd::editor::debug_viz
