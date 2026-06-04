// =============================================================================
// CHROMODYNAMIC — engine/ui/editor/panel_debug_viz/src/DebugViz.cpp
//
// phase696 — cd::editor::debug_viz::DebugVizOverlay implementation.
//
// Refinements over phase684 Sprint-1 placeholders:
//   • Per-overlay toggle() method for F4/F5/F6 hotkey dispatch.
//   • push_frame_sample(float ms) — feeds a 60-slot ring buffer from
//     cpu_marker_overlay / gpu_marker / frame_graph_timeline results.
//   • set_bucket_counts() — live kOpaque/kAlphaMask/kAlphaBlend quad counts
//     from the DrawBatcher state, used by kAlphaBucket proportional bars.
//   • draw_sparkline() — 1 px quads per sample; accent_warning colour when
//     the sample exceeds kBudgetMs (16.6 ms / 60 fps target).
//   • kAlphaBucket bars are now proportional to bucket_counts_ totals; fall
//     back to equal-thirds when total == 0 (first frame before any data).
//
// Sprint-2 (future): replace placeholder tiles with real G-buffer texture
// samples once cd::render::framegraph exposes depth / normal / bucket textures
// as bindable handles in the editor overlay pipeline.
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
    samples_.fill(0.0F);
}

// ---------------------------------------------------------------------------
// Hotkey / Toggle API
// ---------------------------------------------------------------------------

void DebugVizOverlay::set_visible(bool visible) noexcept
{
    visible_ = visible;
}

bool DebugVizOverlay::toggle() noexcept
{
    visible_ = !visible_;
    return visible_;
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
// Sample Feed API
// ---------------------------------------------------------------------------

void DebugVizOverlay::push_frame_sample(float ms) noexcept
{
    samples_[sample_head_] = ms;
    sample_head_ = (sample_head_ + 1U) % kSparklineCapacity;
    if (sample_count_ < kSparklineCapacity)
    {
        ++sample_count_;
    }
}

void DebugVizOverlay::set_bucket_counts(std::uint32_t opaque,
                                        std::uint32_t mask,
                                        std::uint32_t blend) noexcept
{
    bucket_counts_.opaque = opaque;
    bucket_counts_.mask   = mask;
    bucket_counts_.blend  = blend;
}

const BucketCounts& DebugVizOverlay::bucket_counts() const noexcept
{
    return bucket_counts_;
}

std::size_t DebugVizOverlay::sample_count() const noexcept
{
    return sample_count_;
}

float DebugVizOverlay::sample_at(std::size_t i) const noexcept
{
    if (i >= sample_count_) { return 0.0F; }
    // Oldest sample is at (sample_head_ - sample_count_) mod capacity.
    const std::size_t start = (sample_head_ + kSparklineCapacity - sample_count_)
                              % kSparklineCapacity;
    return samples_[(start + i) % kSparklineCapacity];
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
                           const cd::ui::widgets::Rect&   bounds,
                           const cd::ui::widgets::Theme&  theme) const
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

    // ---- 4. Kind-specific content + sparkline overlay -----------------------
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

    // ---- 5. Sparkline overlay (bottom kSparkH px strip) ---------------------
    draw_sparkline(batcher, content, theme);
}

// ---------------------------------------------------------------------------
// kDepth — 8 horizontal grayscale bands: white (top) → black (bottom)
// ---------------------------------------------------------------------------
void DebugVizOverlay::draw_depth(cd::ui::renderer::DrawBatcher& batcher,
                                 const cd::ui::widgets::Rect&   content) const
{
    static constexpr int kBands = 8;
    // Render only in the upper portion (leave bottom for sparkline).
    const float tile_h = std::max(content.h - kSparkH, 1.0F);
    const float band_h = tile_h / static_cast<float>(kBands);

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
// Three-band approximation:
//   left  third  — R channel dominant  (high R, mid G, mid B)
//   centre third — G channel dominant  (mid R, high G, mid B)
//   right  third — B channel dominant  (mid R, mid G, high B)
// ---------------------------------------------------------------------------
void DebugVizOverlay::draw_normal(cd::ui::renderer::DrawBatcher& batcher,
                                  const cd::ui::widgets::Rect&   content) const
{
    const float third_w = content.w / 3.0F;
    // Render only in the upper portion (leave bottom for sparkline).
    const float tile_h = std::max(content.h - kSparkH, 1.0F);

    // Left third — R dominant (facing +X ≈ orange/red in normal-map space).
    batcher.quad(content.x, content.y, third_w, tile_h,
                 cd::ui::renderer::Color { 230U, 128U, 128U, 220U });

    // Centre third — G dominant (facing +Y ≈ green in normal-map space).
    batcher.quad(content.x + third_w, content.y, third_w, tile_h,
                 cd::ui::renderer::Color { 128U, 230U, 128U, 220U });

    // Right third — B dominant (facing +Z ≈ blue in normal-map space;
    // the classic "facing forward" teal-blue of screen-space normals).
    batcher.quad(content.x + 2.0F * third_w, content.y, third_w, tile_h,
                 cd::ui::renderer::Color { 128U, 200U, 230U, 220U });

    // Thin vertical separators to make the band structure legible.
    const cd::ui::renderer::Color sep { 18U, 18U, 22U, 200U };
    batcher.quad(content.x + third_w - 0.5F, content.y, 1.0F, tile_h, sep);
    batcher.quad(content.x + 2.0F * third_w - 0.5F, content.y, 1.0F, tile_h, sep);
}

// ---------------------------------------------------------------------------
// kAlphaBucket — proportional colour bars from bucket_counts_.
//
//   kOpaque     → green  ( 88, 200,  88)  — width proportional to opaque count.
//   kAlphaMask  → yellow (220, 200,  60)  — width proportional to mask count.
//   kAlphaBlend → red    (220,  60,  60)  — width proportional to blend count.
//
// Falls back to equal thirds when total == 0 (no data yet).
// ---------------------------------------------------------------------------
void DebugVizOverlay::draw_alpha_bucket(cd::ui::renderer::DrawBatcher& batcher,
                                        const cd::ui::widgets::Rect&   content) const
{
    // Render only in the upper portion (leave bottom for sparkline).
    const float tile_h = std::max(content.h - kSparkH, 1.0F);

    const std::uint32_t total = bucket_counts_.total();

    float opaque_w = 0.0F;
    float mask_w   = 0.0F;
    float blend_w  = 0.0F;

    if (total > 0U)
    {
        const float scale = content.w / static_cast<float>(total);
        opaque_w = static_cast<float>(bucket_counts_.opaque) * scale;
        mask_w   = static_cast<float>(bucket_counts_.mask)   * scale;
        blend_w  = content.w - opaque_w - mask_w;  // remainder avoids float drift
        blend_w  = std::max(blend_w, 0.0F);
    }
    else
    {
        // No data — equal thirds as Sprint-1 fallback.
        opaque_w = content.w / 3.0F;
        mask_w   = content.w / 3.0F;
        blend_w  = content.w - opaque_w - mask_w;
    }

    float bar_x = content.x;

    // kOpaque — green.
    if (opaque_w > 0.0F)
    {
        batcher.quad(bar_x, content.y, opaque_w, tile_h,
                     cd::ui::renderer::Color { 88U, 200U, 88U, 220U });
        bar_x += opaque_w;
    }

    // kAlphaMask — yellow.
    if (mask_w > 0.0F)
    {
        batcher.quad(bar_x, content.y, mask_w, tile_h,
                     cd::ui::renderer::Color { 220U, 200U, 60U, 220U });
        bar_x += mask_w;
    }

    // kAlphaBlend — red.
    if (blend_w > 0.0F)
    {
        batcher.quad(bar_x, content.y, blend_w, tile_h,
                     cd::ui::renderer::Color { 220U, 60U, 60U, 220U });
    }

    // Thin vertical separators (dark) between bucket bands — only draw when
    // the bar has non-zero width so the separator is visible.
    const cd::ui::renderer::Color sep { 18U, 18U, 22U, 200U };
    if (opaque_w > 1.0F)
    {
        batcher.quad(content.x + opaque_w - 0.5F, content.y, 1.0F, tile_h, sep);
    }
    if (mask_w > 1.0F)
    {
        batcher.quad(content.x + opaque_w + mask_w - 0.5F, content.y, 1.0F, tile_h, sep);
    }
}

// ---------------------------------------------------------------------------
// draw_sparkline — 60-frame ms budget chart in the bottom kSparkH px strip.
//
// Each sample maps to a segment 1 px tall, positioned at a y offset within
// the strip proportional to the sample value (clamped to [0, 2*kBudgetMs]).
// Segments exceeding kBudgetMs are drawn in theme.accent_warning (amber);
// in-budget segments are drawn in a dim green.
//
// Uses quad() as a 1 px-tall rect — DrawBatcher has no line() primitive.
// ---------------------------------------------------------------------------
void DebugVizOverlay::draw_sparkline(cd::ui::renderer::DrawBatcher& batcher,
                                     const cd::ui::widgets::Rect&   content,
                                     const cd::ui::widgets::Theme&  theme) const
{
    if (sample_count_ == 0U) { return; }
    if (content.w <= 0.0F || kSparkH <= 0.0F) { return; }

    // Sparkline strip lives at the bottom of content.
    const float strip_y = content.y + content.h - kSparkH;
    if (strip_y < content.y) { return; }

    // Dark background for the sparkline strip.
    batcher.quad(content.x, strip_y, content.w, kSparkH,
                 cd::ui::renderer::Color { 10U, 10U, 14U, 200U });

    // Budget reference line (dim) at the halfway mark of the strip.
    const float budget_y = strip_y + kSparkH * 0.5F;
    batcher.quad(content.x, budget_y, content.w, 1.0F,
                 cd::ui::renderer::Color { 80U, 80U, 80U, 120U });

    // Segment width: fit all kSparklineCapacity columns into content.w.
    const float seg_w = content.w / static_cast<float>(kSparklineCapacity);
    if (seg_w < 0.5F) { return; }  // too narrow to draw

    // Max visible ms value: 2 × budget gives headroom for overruns.
    constexpr float kMaxMs = kBudgetMs * 2.0F;

    // In-budget colour: dim green.
    const cd::ui::renderer::Color ok_color { 60U, 180U, 80U, 200U };
    // Over-budget colour: theme accent_warning (amber).
    const cd::ui::renderer::Color warn_color {
        theme.accent_warning.r,
        theme.accent_warning.g,
        theme.accent_warning.b,
        200U
    };

    for (std::size_t i = 0U; i < sample_count_; ++i)
    {
        const float ms   = sample_at(i);
        const float norm = std::clamp(ms / kMaxMs, 0.0F, 1.0F);
        // Bar height proportional to sample value (bottom-up).
        const float bar_h  = std::max(norm * kSparkH, 1.0F);
        const float bar_y  = strip_y + kSparkH - bar_h;
        const float bar_x  = content.x + static_cast<float>(i) * seg_w;

        const auto& col = (ms > kBudgetMs) ? warn_color : ok_color;
        batcher.quad(bar_x, bar_y, seg_w, bar_h, col);
    }
}

}  // namespace cd::editor::debug_viz
