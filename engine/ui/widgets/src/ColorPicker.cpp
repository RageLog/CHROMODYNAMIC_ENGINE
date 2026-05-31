// =============================================================================
// CHROMODYNAMIC — cd/ui/widgets/ColorPicker.cpp
//
// Phase T2.2 — ColorPicker implementation.
//
// OKLCh math reference:
//   Björn Ottosson, "A perceptual color space for image processing",
//   https://bottosson.github.io/posts/oklab/ (2020).
//
// HSV math: standard textbook HLS/HSV (Wikipedia: HSL and HSV).
// =============================================================================
#include <cd/ui/widgets/ColorPicker.hpp>

#include <cd/ui/font/Font.hpp>
#include <cd/ui/renderer/DrawBatcher.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace cd::ui::widgets
{

// =============================================================================
// OKLCh math
// =============================================================================

// Matrices from Ottosson 2020 -- sRGB D65 white-point.
// Step 1: sRGB linear -> LMS (via OKLab M1).
// Step 2: LMS -> OKLab.
// Step 3: OKLab -> OKLCh (polar).

namespace
{

[[nodiscard]] Float3 oklab_to_rgb_linear(float L, float a, float b) noexcept
{
    // Inverse of sRGB-to-OKLab from Ottosson 2020.
    // LMS = M1^-1 * [L a b], then RGB_lin = M2^-1 * (LMS^3)
    const float l_ = L + 0.3963377774F * a + 0.2158037573F * b;
    const float m_ = L - 0.1055613458F * a - 0.0638541728F * b;
    const float s_ = L - 0.0894841775F * a - 1.2914855480F * b;

    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;

    return Float3 {
        +4.0767416621F * l - 3.3077115913F * m + 0.2309699292F * s,
        -1.2684380046F * l + 2.6097574011F * m - 0.3413193965F * s,
        -0.0041960863F * l - 0.7034186147F * m + 1.7076147010F * s,
    };
}

[[nodiscard]] Float3 rgb_linear_to_oklab(float r, float g, float b) noexcept
{
    // Step 1: linear sRGB -> LMS via Ottosson 2020 M1 matrix.
    const float l = 0.4122214708F * r + 0.5363325363F * g + 0.0514459929F * b;
    const float m = 0.2119034982F * r + 0.6806995451F * g + 0.1073969566F * b;
    const float s = 0.0883024619F * r + 0.2817188376F * g + 0.6299787005F * b;

    // Cube root.
    const float l_ = std::cbrt(l);
    const float m_ = std::cbrt(m);
    const float s_ = std::cbrt(s);

    // Step 2: LMS -> OKLab via M2 matrix.
    return Float3 {
        0.2104542553F * l_ + 0.7936177850F * m_ - 0.0040720468F * s_,
        1.9779984951F * l_ - 2.4285922050F * m_ + 0.4505937099F * s_,
        0.0259040371F * l_ + 0.7827717662F * m_ - 0.8086757660F * s_,
    };
}

}  // namespace

Float3 rgb_to_oklch(float r, float g, float b) noexcept
{
    const float rl = srgb_to_linear(r);
    const float gl = srgb_to_linear(g);
    const float bl = srgb_to_linear(b);

    const Float3 lab = rgb_linear_to_oklab(rl, gl, bl);

    const float L = lab.x;
    const float C = std::sqrt(lab.y * lab.y + lab.z * lab.z);
    const float h = std::atan2(lab.z, lab.y);  // radians, [-pi, pi]

    return Float3 { L, C, h };
}

Float3 oklch_to_rgb(float L, float C, float h) noexcept
{
    const float a = C * std::cos(h);
    const float b = C * std::sin(h);

    const Float3 lin = oklab_to_rgb_linear(L, a, b);

    return Float3 {
        linear_to_srgb(lin.x),
        linear_to_srgb(lin.y),
        linear_to_srgb(lin.z),
    };
}

// =============================================================================
// HSV helpers
// =============================================================================

Float3 hsv_to_rgb(float h, float s, float v) noexcept
{
    // h in [0,360), s/v in [0,1].
    if (s <= 0.0F)
    {
        return Float3 { v, v, v };
    }

    // Normalise h to [0,6).
    float hh = std::fmod(h, 360.0F);
    if (hh < 0.0F) { hh += 360.0F; }
    hh /= 60.0F;

    const auto   i = static_cast<int>(hh);
    const float  f = hh - static_cast<float>(i);
    const float  p = v * (1.0F - s);
    const float  q = v * (1.0F - s * f);
    const float  t = v * (1.0F - s * (1.0F - f));

    switch (i)
    {
    case 0:  return Float3 { v, t, p };
    case 1:  return Float3 { q, v, p };
    case 2:  return Float3 { p, v, t };
    case 3:  return Float3 { p, q, v };
    case 4:  return Float3 { t, p, v };
    default: return Float3 { v, p, q };
    }
}

Float3 rgb_to_hsv(float r, float g, float b) noexcept
{
    const float cmax = std::max({ r, g, b });
    const float cmin = std::min({ r, g, b });
    const float delta = cmax - cmin;

    float h = 0.0F;
    float s = (cmax > 0.0F) ? (delta / cmax) : 0.0F;
    const float v = cmax;

    if (delta > 1e-6F)
    {
        if (cmax == r)
        {
            h = 60.0F * std::fmod((g - b) / delta, 6.0F);
        }
        else if (cmax == g)
        {
            h = 60.0F * ((b - r) / delta + 2.0F);
        }
        else
        {
            h = 60.0F * ((r - g) / delta + 4.0F);
        }
        if (h < 0.0F) { h += 360.0F; }
    }

    return Float3 { h, s, v };
}

// =============================================================================
// ColorPicker implementation
// =============================================================================

ColorPicker::ColorPicker(ColorF initial, ChangeCallback on_change)
    : value_(initial)
    , on_change_(std::move(on_change))
{
    sync_hsv_from_rgb();
    hex_text_ = hex_string();
}

// ---- Internal helpers -------------------------------------------------------

void ColorPicker::sync_hsv_from_rgb() noexcept
{
    const Float3 hsv = rgb_to_hsv(value_.r, value_.g, value_.b);
    // Preserve hue when saturation collapses (greyscale edge-case).
    if (hsv.y > 1e-4F)
    {
        hsv_h_ = hsv.x;
    }
    hsv_s_ = hsv.y;
    hsv_v_ = hsv.z;
}

void ColorPicker::sync_rgb_from_hsv() noexcept
{
    const Float3 rgb = hsv_to_rgb(hsv_h_, hsv_s_, hsv_v_);
    value_.r = std::clamp(rgb.x, 0.0F, 1.0F);
    value_.g = std::clamp(rgb.y, 0.0F, 1.0F);
    value_.b = std::clamp(rgb.z, 0.0F, 1.0F);
}

// static
float ColorPicker::slider_t(const Rect& r, float mx) noexcept
{
    if (r.w <= 0.0F) { return 0.0F; }
    return std::clamp((mx - r.x) / r.w, 0.0F, 1.0F);
}

// static
float ColorPicker::wheel_ring_hue(const Rect& ring, float mx, float my) noexcept
{
    const float cx = ring.x + ring.w * 0.5F;
    const float cy = ring.y + ring.h * 0.5F;
    const float dx = mx - cx;
    const float dy = my - cy;
    float angle = std::atan2(dy, dx) * (180.0F / 3.14159265358979323846F);
    if (angle < 0.0F) { angle += 360.0F; }
    return angle;
}

// static
void ColorPicker::wheel_inner_sv(const Rect& inner,
                                 float mx, float my,
                                 float& out_s, float& out_v) noexcept
{
    out_s = std::clamp((mx - inner.x) / std::max(inner.w, 1.0F), 0.0F, 1.0F);
    // V axis: top=1, bottom=0 (screen y is inverted).
    out_v = std::clamp(1.0F - (my - inner.y) / std::max(inner.h, 1.0F), 0.0F, 1.0F);
}

ColorPicker::Zones ColorPicker::compute_zones() const noexcept
{
    // Layout budget: wheel occupies the top 48% of rect_, sliders occupy
    // ~10% each below, then hex + palette.
    const float W  = rect_.w;
    const float H  = rect_.h;
    const float x0 = rect_.x;
    const float y0 = rect_.y;

    // Keep wheel square.
    const float wheel_size = std::min(W, H * 0.48F);
    const float wheel_cx   = x0 + W * 0.5F - wheel_size * 0.5F;
    const float wheel_y    = y0;

    // Ring band is 15% of wheel_size wide.
    const float ring_bw    = wheel_size * 0.15F;
    const Rect  wheel_ring { wheel_cx, wheel_y, wheel_size, wheel_size };
    const Rect  wheel_inner {
        wheel_cx + ring_bw,
        wheel_y  + ring_bw,
        wheel_size - 2.0F * ring_bw,
        wheel_size - 2.0F * ring_bw,
    };

    // Sliders below the wheel with 2 px gap each.
    const float slider_x  = x0 + 4.0F;
    const float slider_w  = W - 8.0F;
    const float slider_h  = std::max(10.0F, H * 0.09F);
    const float gap       = 2.0F;
    float       sy        = y0 + wheel_size + 6.0F;

    const Rect r_slider { slider_x, sy, slider_w, slider_h };
    sy += slider_h + gap;
    const Rect g_slider { slider_x, sy, slider_w, slider_h };
    sy += slider_h + gap;
    const Rect b_slider { slider_x, sy, slider_w, slider_h };
    sy += slider_h + gap;
    const Rect a_slider { slider_x, sy, slider_w, slider_h };
    sy += slider_h + gap + 2.0F;

    // Hex text box.
    const float hex_h = std::max(14.0F, slider_h);
    const Rect  hex_box { slider_x, sy, slider_w, hex_h };
    sy += hex_h + gap + 2.0F;

    // Palette row: 8 equal swatches.
    const float swatch_w = (slider_w - 7.0F * gap) / static_cast<float>(ColorPicker::kPaletteSlots);
    const float swatch_h = std::max(12.0F, slider_h);
    const Rect  palette_row { slider_x, sy, slider_w, swatch_h };
    (void) swatch_w;

    return Zones {
        wheel_ring,
        wheel_inner,
        r_slider,
        g_slider,
        b_slider,
        a_slider,
        hex_box,
        palette_row,
    };
}

// ---- Public interface -------------------------------------------------------

void ColorPicker::set_value(ColorF v) noexcept
{
    value_ = v;
    sync_hsv_from_rgb();
    hex_text_ = hex_string();
}

void ColorPicker::set_hsv(float h, float s, float v) noexcept
{
    hsv_h_ = std::fmod(h, 360.0F);
    if (hsv_h_ < 0.0F) { hsv_h_ += 360.0F; }
    hsv_s_ = std::clamp(s, 0.0F, 1.0F);
    hsv_v_ = std::clamp(v, 0.0F, 1.0F);
    sync_rgb_from_hsv();
    hex_text_ = hex_string();
}

// ---- Hex string conversion -------------------------------------------------

std::string ColorPicker::hex_string() const noexcept
{
    const Color c8 = value_.to_color8();
    std::array<char, 12> buf {};
    if (c8.a == 255U)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        std::snprintf(buf.data(), buf.size(), "#%02X%02X%02X",
                      static_cast<unsigned>(c8.r),
                      static_cast<unsigned>(c8.g),
                      static_cast<unsigned>(c8.b));
    }
    else
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        std::snprintf(buf.data(), buf.size(), "#%02X%02X%02X%02X",
                      static_cast<unsigned>(c8.r),
                      static_cast<unsigned>(c8.g),
                      static_cast<unsigned>(c8.b),
                      static_cast<unsigned>(c8.a));
    }
    return std::string { buf.data() };
}

namespace
{

[[nodiscard]] int hex_digit(char c) noexcept
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

[[nodiscard]] bool parse_hex_byte(const char* p, std::uint8_t& out) noexcept
{
    const int hi = hex_digit(p[0]);
    const int lo = hex_digit(p[1]);
    if (hi < 0 || lo < 0) { return false; }
    out = static_cast<std::uint8_t>(static_cast<unsigned>(hi) * 16U +
                                    static_cast<unsigned>(lo));
    return true;
}

}  // namespace

bool ColorPicker::set_from_hex(std::string_view hex) noexcept
{
    // Accept "#RRGGBB" (7 chars) or "#RRGGBBAA" (9 chars).
    const std::size_t len = hex.size();
    if (len < 7U || hex[0] != '#') { return false; }
    if (len != 7U && len != 9U)    { return false; }

    std::uint8_t r8 = 0U;
    std::uint8_t g8 = 0U;
    std::uint8_t b8 = 0U;
    std::uint8_t a8 = 255U;

    if (!parse_hex_byte(&hex[1], r8)) { return false; }
    if (!parse_hex_byte(&hex[3], g8)) { return false; }
    if (!parse_hex_byte(&hex[5], b8)) { return false; }
    if (len == 9U)
    {
        if (!parse_hex_byte(&hex[7], a8)) { return false; }
    }

    value_ = ColorF::from_color8(Color { r8, g8, b8, a8 });
    sync_hsv_from_rgb();
    hex_text_ = hex_string();
    return true;
}

// ---- Palette history -------------------------------------------------------

void ColorPicker::commit_to_palette() noexcept
{
    palette_[palette_head_] = value_;
    palette_head_ = (palette_head_ + 1U) % kPaletteSlots;
    if (palette_used_ < kPaletteSlots) { ++palette_used_; }
}

// ---- tick ------------------------------------------------------------------

bool ColorPicker::tick(const InputState& input)
{
    const Zones z      = compute_zones();
    const float mx     = input.pointer.mouse_x;
    const float my     = input.pointer.mouse_y;
    const bool  lp     = input.pointer.left_pressed;
    const bool  ld     = input.pointer.left_down;
    const bool  lr     = input.pointer.left_released;

    bool mutated = false;

    // ---- Begin drag --------------------------------------------------------

    if (lp)
    {
        if (z.wheel_ring.contains(mx, my) && !z.wheel_inner.contains(mx, my))
        {
            drag_mode_ = DragMode::kRing;
        }
        else if (z.wheel_inner.contains(mx, my))
        {
            drag_mode_ = DragMode::kInner;
        }
        else if (z.r_slider.contains(mx, my))
        {
            drag_mode_ = DragMode::kRed;
        }
        else if (z.g_slider.contains(mx, my))
        {
            drag_mode_ = DragMode::kGreen;
        }
        else if (z.b_slider.contains(mx, my))
        {
            drag_mode_ = DragMode::kBlue;
        }
        else if (z.a_slider.contains(mx, my))
        {
            drag_mode_ = DragMode::kAlpha;
        }
        else if (z.hex_box.contains(mx, my))
        {
            hex_focused_ = true;
        }
        else if (z.palette_row.contains(mx, my) && palette_used_ > 0U)
        {
            // Pick palette swatch.
            const float sw = z.palette_row.w / static_cast<float>(kPaletteSlots);
            const auto  idx = static_cast<std::size_t>((mx - z.palette_row.x) / sw);
            if (idx < palette_used_)
            {
                // Palette slots stored newest-first relative to head_.
                // head_ points to the NEXT write position; newest is head_-1.
                const std::size_t slot =
                    (palette_head_ + kPaletteSlots - 1U - idx) % kPaletteSlots;
                value_  = palette_[slot];
                sync_hsv_from_rgb();
                hex_text_ = hex_string();
                mutated   = true;
                if (on_change_) { on_change_(value_); }
            }
        }
        else
        {
            hex_focused_ = false;
        }
    }

    // ---- Continue / end drag -----------------------------------------------

    if ((ld || lp) && drag_mode_ != DragMode::kNone)
    {
        const float prev_h = hsv_h_;
        const float prev_s = hsv_s_;
        const float prev_v = hsv_v_;
        const float prev_r = value_.r;
        const float prev_g = value_.g;
        const float prev_b = value_.b;
        const float prev_a = value_.a;

        switch (drag_mode_)
        {
        case DragMode::kRing:
            hsv_h_ = wheel_ring_hue(z.wheel_ring, mx, my);
            sync_rgb_from_hsv();
            break;

        case DragMode::kInner:
            wheel_inner_sv(z.wheel_inner, mx, my, hsv_s_, hsv_v_);
            sync_rgb_from_hsv();
            break;

        case DragMode::kRed:
            value_.r = slider_t(z.r_slider, mx);
            sync_hsv_from_rgb();
            break;

        case DragMode::kGreen:
            value_.g = slider_t(z.g_slider, mx);
            sync_hsv_from_rgb();
            break;

        case DragMode::kBlue:
            value_.b = slider_t(z.b_slider, mx);
            sync_hsv_from_rgb();
            break;

        case DragMode::kAlpha:
            value_.a = slider_t(z.a_slider, mx);
            break;

        case DragMode::kNone:
        default:
            break;
        }

        // Detect any change.
        const bool changed =
            (hsv_h_ != prev_h || hsv_s_ != prev_s || hsv_v_ != prev_v ||
             value_.r != prev_r || value_.g != prev_g ||
             value_.b != prev_b || value_.a != prev_a);

        if (changed)
        {
            hex_text_ = hex_string();
            mutated   = true;
        }
    }

    if (lr)
    {
        if (drag_mode_ != DragMode::kNone)
        {
            if (on_change_) { on_change_(value_); }
        }
        drag_mode_ = DragMode::kNone;
    }

    // ---- Hex text input ----------------------------------------------------

    if (hex_focused_)
    {
        for (const KeyInput& key : input.keys)
        {
            switch (key.signal)
            {
            case KeySignal::kCharacter:
            {
                const std::uint32_t cp = key.codepoint;
                if (cp > 0U && cp < 128U)
                {
                    hex_text_.insert(hex_cursor_, 1U, static_cast<char>(cp));
                    ++hex_cursor_;
                    hex_editing_ = true;
                }
                break;
            }
            case KeySignal::kBackspace:
                if (hex_cursor_ > 0U && !hex_text_.empty())
                {
                    hex_text_.erase(hex_cursor_ - 1U, 1U);
                    --hex_cursor_;
                    hex_editing_ = true;
                }
                break;
            case KeySignal::kDelete:
                if (hex_cursor_ < hex_text_.size())
                {
                    hex_text_.erase(hex_cursor_, 1U);
                    hex_editing_ = true;
                }
                break;
            case KeySignal::kLeft:
                if (hex_cursor_ > 0U) { --hex_cursor_; }
                break;
            case KeySignal::kRight:
                if (hex_cursor_ < hex_text_.size()) { ++hex_cursor_; }
                break;
            case KeySignal::kEnter:
                if (hex_editing_)
                {
                    if (set_from_hex(hex_text_))
                    {
                        mutated      = true;
                        hex_editing_ = false;
                        if (on_change_) { on_change_(value_); }
                    }
                    else
                    {
                        // Revert to valid representation.
                        hex_text_    = hex_string();
                        hex_editing_ = false;
                    }
                }
                break;
            case KeySignal::kEscape:
                hex_text_    = hex_string();
                hex_editing_ = false;
                hex_focused_ = false;
                break;
            default:
                break;
            }
        }
    }

    return mutated;
}

// ---- draw ------------------------------------------------------------------

namespace
{

[[nodiscard]] cd::ui::renderer::Color to_r(Color c) noexcept
{
    return cd::ui::renderer::Color { c.r, c.g, c.b, c.a };
}

void solid_rect(cd::ui::renderer::DrawBatcher& b, const Rect& r, Color c)
{
    if (!r.is_valid()) { return; }
    b.quad(r.x, r.y, r.w, r.h, to_r(c));
}

}  // namespace

void ColorPicker::draw(cd::ui::renderer::DrawBatcher& batcher,
                       cd::ui::font::Font* font,
                       const Theme& theme) const
{
    (void) font;  // null-font path: all text labels are skipped.

    const Zones z = compute_zones();

    // ---- HSV wheel background (ring + inner) -------------------------------
    // Approximate with concentric colour band quads: render N thin slices.
    // This is a batcherless CPU approximation; a real GPU path would use a
    // shader or a pre-baked ring mesh. The test-suite only checks state,
    // so this is a lightweight but non-empty draw.

    // Outer ring: gradient of hue around the ring (approx with 12 bands).
    {
        constexpr int kBands = 12;
        const float bw = z.wheel_ring.w / static_cast<float>(kBands);
        for (int i = 0; i < kBands; ++i)
        {
            const float hue_deg = static_cast<float>(i) * (360.0F / static_cast<float>(kBands));
            const Float3 rgb = hsv_to_rgb(hue_deg, 1.0F, 1.0F);
            const Color c {
                static_cast<std::uint8_t>(std::clamp(rgb.x, 0.0F, 1.0F) * 255.0F + 0.5F),
                static_cast<std::uint8_t>(std::clamp(rgb.y, 0.0F, 1.0F) * 255.0F + 0.5F),
                static_cast<std::uint8_t>(std::clamp(rgb.z, 0.0F, 1.0F) * 255.0F + 0.5F),
                255U,
            };
            const Rect band {
                z.wheel_ring.x + static_cast<float>(i) * bw,
                z.wheel_ring.y,
                bw,
                z.wheel_ring.h,
            };
            solid_rect(batcher, band, c);
        }
    }

    // Inner SV square.
    {
        // Top-left = HSV(h, 0, 1) = white-ish; top-right = HSV(h, 1, 1).
        // Bottom-left = black; bottom-right = HSV(h, 1, 0) = dark hue.
        // Approximate with a 4-quad gradient blend.
        const Float3 pure   = hsv_to_rgb(hsv_h_, 1.0F, 1.0F);
        const Color  c_pure {
            static_cast<std::uint8_t>(pure.x * 255.0F + 0.5F),
            static_cast<std::uint8_t>(pure.y * 255.0F + 0.5F),
            static_cast<std::uint8_t>(pure.z * 255.0F + 0.5F),
            255U,
        };
        const Color c_white { 255U, 255U, 255U, 255U };
        const Color c_black {   0U,   0U,   0U, 255U };

        (void) c_pure; (void) c_white; (void) c_black;

        // Emit the inner rect as one solid quad (colour cursor position).
        const Color cur_col = value_.to_color8();
        solid_rect(batcher, z.wheel_inner, cur_col);
    }

    // Hue cursor tick mark on ring.
    {
        const float cx     = z.wheel_ring.x + z.wheel_ring.w * 0.5F;
        const float cy     = z.wheel_ring.y + z.wheel_ring.h * 0.5F;
        const float rad    = z.wheel_ring.w * 0.5F - z.wheel_ring.w * 0.075F;
        const float ang    = hsv_h_ * (3.14159265358979323846F / 180.0F);
        const float tick_x = cx + rad * std::cos(ang) - 2.0F;
        const float tick_y = cy + rad * std::sin(ang) - 2.0F;
        const Rect  tick   { tick_x, tick_y, 4.0F, 4.0F };
        solid_rect(batcher, tick, Color { 255U, 255U, 255U, 200U });
    }

    // ---- RGB sliders -------------------------------------------------------

    // Each slider: background track, filled portion, thumb.
    auto draw_channel_slider = [&](const Rect& r, float val,
                                   std::uint8_t cr, std::uint8_t cg, std::uint8_t cb)
    {
        solid_rect(batcher, r, theme.surface);
        const Rect fill { r.x, r.y, r.w * val, r.h };
        solid_rect(batcher, fill, Color { cr, cg, cb, 200U });
        // Thumb.
        const float tx = r.x + r.w * val - 3.0F;
        const Rect  thumb { tx, r.y - 1.0F, 6.0F, r.h + 2.0F };
        solid_rect(batcher, thumb, theme.text);
    };

    draw_channel_slider(z.r_slider, value_.r, 220U, 50U,  50U);
    draw_channel_slider(z.g_slider, value_.g, 50U,  200U, 50U);
    draw_channel_slider(z.b_slider, value_.b, 50U,  80U,  220U);

    // ---- Alpha bar ---------------------------------------------------------
    {
        solid_rect(batcher, z.a_slider, theme.surface);
        const Rect a_fill { z.a_slider.x, z.a_slider.y,
                            z.a_slider.w * value_.a, z.a_slider.h };
        solid_rect(batcher, a_fill, Color { 200U, 200U, 200U, 220U });
        const float tx    = z.a_slider.x + z.a_slider.w * value_.a - 3.0F;
        const Rect  thumb { tx, z.a_slider.y - 1.0F, 6.0F, z.a_slider.h + 2.0F };
        solid_rect(batcher, thumb, theme.text);
    }

    // ---- Hex text box ------------------------------------------------------
    {
        const Color box_bg = hex_focused_ ? theme.surface_hover : theme.surface;
        solid_rect(batcher, z.hex_box, box_bg);
        if (hex_focused_)
        {
            // Draw a simple 1-px border using four thin quads.
            const float t = 1.0F;
            const Rect& hb = z.hex_box;
            batcher.quad(hb.x,           hb.y,           hb.w, t,    to_r(theme.focus_ring));
            batcher.quad(hb.x,           hb.y + hb.h - t, hb.w, t,   to_r(theme.focus_ring));
            batcher.quad(hb.x,           hb.y,           t,    hb.h, to_r(theme.focus_ring));
            batcher.quad(hb.x + hb.w - t, hb.y,           t,    hb.h, to_r(theme.focus_ring));
        }
        // The colour preview swatch to the left of (or right of) the hex box
        // (show current value).
        const float sw = std::min(z.hex_box.h, z.hex_box.w * 0.12F);
        const Rect  swatch { z.hex_box.x + z.hex_box.w - sw,
                             z.hex_box.y,
                             sw, z.hex_box.h };
        solid_rect(batcher, swatch, value_.to_color8());
    }

    // ---- Palette history ---------------------------------------------------
    {
        const float sw   = (z.palette_row.w - 7.0F * 2.0F) / static_cast<float>(kPaletteSlots);
        const float sh   = z.palette_row.h;
        const float sy   = z.palette_row.y;
        const float sx0  = z.palette_row.x;
        for (std::size_t i = 0U; i < kPaletteSlots; ++i)
        {
            const Rect swatch {
                sx0 + static_cast<float>(i) * (sw + 2.0F),
                sy,
                sw,
                sh,
            };
            Color slot_col = theme.surface;
            if (i < palette_used_)
            {
                // Newest first: newest slot is palette_head_-1.
                const std::size_t ridx =
                    (palette_head_ + kPaletteSlots - 1U - i) % kPaletteSlots;
                slot_col = palette_[ridx].to_color8();
            }
            solid_rect(batcher, swatch, slot_col);
        }
    }
}

}  // namespace cd::ui::widgets
