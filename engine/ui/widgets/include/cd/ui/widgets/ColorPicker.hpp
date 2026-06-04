// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/ColorPicker.hpp
//
// Phase T2.2 — HSV wheel + RGB sliders + hex input + OKLCh display +
// 8-slot palette history color picker widget.
//
// Design contract:
//   * ColorPicker owns its rect (set externally by layout via `set_rect`).
//   * tick(input)  — pointer + key driven state transitions (no GPU).
//   * draw(batcher, font, theme) — emits draw commands; tolerates null font.
//   * on_change callback fires whenever the RGBA value commits (drag-end,
//     hex parse, palette slot pick). During drag-in-progress the value
//     updates but the callback is suppressed until release (avoids flooding
//     the callsite with intermediate values).
//
// Sub-regions (all positioned relative to `rect_`):
//   - HSV wheel ring  : outermost ring for hue, inner disc for saturation/
//                       value (approximated with concentric quads in the
//                       batcherless test path; real GPU path uses the mesh).
//   - RGB strip       : three horizontal sliders (R, G, B) stacked below.
//   - Alpha bar       : fourth horizontal slider for alpha.
//   - Hex text input  : "#RRGGBB" or "#RRGGBBAA" editable field.
//   - Palette history : 8 colour swatches in a row at the bottom.
//
// OKLCh helpers (Björn Ottosson 2020):
//   static float3 rgb_to_oklch(r, g, b)  -> {L, C, h} normalised
//   static float3 oklch_to_rgb(L, C, h)  -> {r, g, b} clamped to [0,1]
//
// Only the round-trip maths + state-machine are exercised by the test
// suite; the draw path is a smoke test (null-font, no GPU).
// =============================================================================
#pragma once

#include <cd/ui/widgets/Widgets.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

namespace cd::ui::widgets
{

// ---- OKLCh / RGB maths helpers --------------------------------------------

/// Linearise a sRGB channel (inverse gamma, sRGB transfer function).
[[nodiscard]] inline float srgb_to_linear(float c) noexcept
{
    if (c <= 0.04045F)
    {
        return c / 12.92F;
    }
    return std::pow((c + 0.055F) / 1.055F, 2.4F);
}

/// Apply sRGB gamma to a linear channel.
[[nodiscard]] inline float linear_to_srgb(float c) noexcept
{
    c = std::clamp(c, 0.0F, 1.0F);
    if (c <= 0.0031308F)
    {
        return c * 12.92F;
    }
    return 1.055F * std::pow(c, 1.0F / 2.4F) - 0.055F;
}

/// Three-float POD used by OKLab / OKLCh helpers.
struct Float3
{
    float x { 0.0F };
    float y { 0.0F };
    float z { 0.0F };
};

/// Convert sRGB [0,1] to OKLCh {L in [0,1], C in [0,~0.4], h in [0,2pi)}.
/// Reference: Björn Ottosson, "A perceptual color space for image processing"
/// https://bottosson.github.io/posts/oklab/ (2020).
[[nodiscard]] Float3 rgb_to_oklch(float r, float g, float b) noexcept;

/// Convert OKLCh {L, C, h} back to sRGB [0,1] (channels clamped).
[[nodiscard]] Float3 oklch_to_rgb(float L, float C, float h) noexcept;

// ---- HSV helpers -----------------------------------------------------------

/// Convert HSV (h in [0,360), s/v in [0,1]) to linear RGB [0,1].
[[nodiscard]] Float3 hsv_to_rgb(float h, float s, float v) noexcept;

/// Convert RGB [0,1] to HSV (h in [0,360), s/v in [0,1]).
[[nodiscard]] Float3 rgb_to_hsv(float r, float g, float b) noexcept;

// ---- RGBA float colour -----------------------------------------------------

/// Floating-point RGBA colour {r,g,b,a} all in [0,1].
struct ColorF
{
    float r { 1.0F };
    float g { 1.0F };
    float b { 1.0F };
    float a { 1.0F };

    /// Convert to the byte Color used by the rest of the widget stack.
    [[nodiscard]] Color to_color8() const noexcept
    {
        return Color {
            static_cast<std::uint8_t>(std::lround(std::clamp(r, 0.0F, 1.0F) * 255.0F)),
            static_cast<std::uint8_t>(std::lround(std::clamp(g, 0.0F, 1.0F) * 255.0F)),
            static_cast<std::uint8_t>(std::lround(std::clamp(b, 0.0F, 1.0F) * 255.0F)),
            static_cast<std::uint8_t>(std::lround(std::clamp(a, 0.0F, 1.0F) * 255.0F)),
        };
    }

    /// Build from byte Color.
    [[nodiscard]] static ColorF from_color8(Color c) noexcept
    {
        return ColorF {
            static_cast<float>(c.r) / 255.0F,
            static_cast<float>(c.g) / 255.0F,
            static_cast<float>(c.b) / 255.0F,
            static_cast<float>(c.a) / 255.0F,
        };
    }
};

// ---- ColorPicker -----------------------------------------------------------

/// HSV wheel + RGB sliders + hex input + alpha bar + 8-slot palette history.
///
/// Layout within `rect_` (top-to-bottom, height budget approximate):
///   [0%  .. 50%]  HSV wheel ring area (concentric rect approximation)
///   [52% .. 65%]  R slider
///   [66% .. 79%]  G slider
///   [80% .. 93%]  B slider
///   [94% .. 107%] Alpha slider (may overflow slightly; callers size rect)
///   [108%.. 120%] Hex text input
///   [122%.. 135%] Palette history row (8 swatches)
///
/// Because the picker is taller than many single-widget rects, callers
/// should size `rect_` to at least 260 px tall for comfortable display.
class ColorPicker
{
public:
    using ChangeCallback = std::function<void(ColorF)>;

    static constexpr std::size_t kPaletteSlots = 8U;

    ColorPicker() = default;
    explicit ColorPicker(ColorF initial, ChangeCallback on_change = {});

    // --- Geometry -----------------------------------------------------------

    void set_rect(Rect r) noexcept { rect_ = r; }
    [[nodiscard]] const Rect& rect() const noexcept { return rect_; }

    // --- Value accessors ----------------------------------------------------

    void set_value(ColorF v) noexcept;
    [[nodiscard]] const ColorF& value() const noexcept { return value_; }

    void set_on_change(ChangeCallback cb) { on_change_ = std::move(cb); }

    // --- Palette history ----------------------------------------------------

    /// Commit the current value to the palette history. The oldest slot is
    /// evicted when the history is full (8 slots). Duplicate consecutive
    /// entries are not deduplicated -- the callsite decides semantics.
    void commit_to_palette() noexcept;

    [[nodiscard]] const std::array<ColorF, kPaletteSlots>& palette() const noexcept
    {
        return palette_;
    }

    [[nodiscard]] std::size_t palette_used() const noexcept { return palette_used_; }

    // --- Hex text -----------------------------------------------------------

    /// Current hex string reflecting value. Includes leading '#'.
    /// Format: "#RRGGBBAA" if alpha < 1, else "#RRGGBB".
    [[nodiscard]] std::string hex_string() const noexcept;

    /// Parse "#RRGGBB" or "#RRGGBBAA". Returns true on success.
    [[nodiscard]] bool set_from_hex(std::string_view hex) noexcept;

    // --- HSV accessors (kept in sync with value_) ---------------------------

    [[nodiscard]] float hue()        const noexcept { return hsv_h_; }  ///< degrees [0,360)
    [[nodiscard]] float saturation() const noexcept { return hsv_s_; }  ///< [0,1]
    [[nodiscard]] float brightness() const noexcept { return hsv_v_; }  ///< [0,1]

    void set_hsv(float h, float s, float v) noexcept;

    // --- Static OKLCh public helpers ----------------------------------------

    [[nodiscard]] static Float3 rgb_to_oklch(float r, float g, float b) noexcept
    {
        return cd::ui::widgets::rgb_to_oklch(r, g, b);
    }

    [[nodiscard]] static Float3 oklch_to_rgb(float L, float C, float h) noexcept
    {
        return cd::ui::widgets::oklch_to_rgb(L, C, h);
    }

    // --- State machine ------------------------------------------------------

    /// Returns true when the value mutated this frame.
    bool tick(const InputState& input);

    // --- Rendering ----------------------------------------------------------

    void draw(cd::ui::renderer::DrawBatcher& batcher,
              cd::ui::font::Font* font,
              const Theme& theme) const;

private:
    // Sub-widget interaction zones derived from rect_ each frame.
    struct Zones
    {
        Rect wheel_ring   {};   ///< outermost ring band (hue)
        Rect wheel_inner  {};   ///< inner disc (SV square approximation)
        Rect r_slider     {};
        Rect g_slider     {};
        Rect b_slider     {};
        Rect a_slider     {};
        Rect hex_box      {};
        Rect palette_row  {};
    };

    [[nodiscard]] Zones compute_zones() const noexcept;

    // Update HSV from value_ RGB.
    void sync_hsv_from_rgb() noexcept;
    // Update value_ RGB from HSV.
    void sync_rgb_from_hsv() noexcept;

    /// Map a mouse x inside a horizontal slider rect to [0,1].
    [[nodiscard]] static float slider_t(const Rect& r, float mx) noexcept;

    /// Map a mouse position inside the wheel_ring to a hue angle in [0,360).
    [[nodiscard]] static float wheel_ring_hue(const Rect& ring, float mx, float my) noexcept;

    /// Map a mouse position inside the wheel_inner to saturation + value.
    static void wheel_inner_sv(const Rect& inner,
                                             float mx, float my,
                                             float& out_s, float& out_v) noexcept;

    // --- State --------------------------------------------------------------

    Rect             rect_           {};
    ColorF           value_          { 1.0F, 1.0F, 1.0F, 1.0F };
    ChangeCallback   on_change_      {};

    // HSV mirror kept in sync with value_ via sync_hsv_from_rgb() /
    // sync_rgb_from_hsv(). Avoids lossy RGB -> HSV round-trips during drag.
    float            hsv_h_          { 0.0F };
    float            hsv_s_          { 0.0F };
    float            hsv_v_          { 1.0F };

    // Drag mode tracking: which zone is being dragged.
    enum class DragMode : std::uint8_t
    {
        kNone   = 0,
        kRing   = 1,
        kInner  = 2,
        kRed    = 3,
        kGreen  = 4,
        kBlue   = 5,
        kAlpha  = 6,
    };

    DragMode drag_mode_    { DragMode::kNone };

    // Hex text input.
    std::string      hex_text_       {};
    bool             hex_focused_    { false };
    bool             hex_editing_    { false }; ///< user has typed chars not yet committed
    std::size_t      hex_cursor_     { 0U };

    // Palette history: circular FIFO of kPaletteSlots.
    std::array<ColorF, kPaletteSlots> palette_ {};
    std::size_t      palette_used_   { 0U };
    std::size_t      palette_head_   { 0U };  ///< next write index (ring)
};

}  // namespace cd::ui::widgets
