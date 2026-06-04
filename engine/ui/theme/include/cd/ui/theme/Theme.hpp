// =============================================================================
// CHROMODYNAMIC -- cd/ui/theme/Theme.hpp
//
// Phase 474 -- cd::ui::theme V2 design token system.
//
// This header is the NEW V2 token-based theme tree. The legacy 2-palette
// stub at engine/ui/include/cd/ui/Theme.hpp is intentionally LEFT IN PLACE
// so existing call sites (terminal UI / dropdown popups) continue to
// compile unchanged. V2 callers (widgets V2, editor V2, sample HUDs)
// include this header from cd/ui/theme/Theme.hpp and pull cd::ui::theme::*.
//
// Design tokens are modelled after Material 3 (Google) and the public
// design-token specification (W3C draft): semantic role names so the
// visual identity can be reskinned without changing widget code, and a
// stable scale (spacing / typography / motion / elevation) shared with
// web design systems so callers porting from Figma / Tailwind / MUI map
// 1:1.
//
// Dependencies: cd::core only (no cd::ui hard dep so headless / asset /
// tooling consumers can use the tokens without pulling the whole UI
// widget tree).
//
// Scope (this header):
//   * ColorToken (linear-friendly RGBA float, [0..1])
//   * Typography (font name, size in pt, line-height multiplier,
//                 letter-spacing in em)
//   * Spacing (5-step semantic scale: xs / sm / md / lg / xl, pixels)
//   * Motion (3 duration buckets: short / medium / long, milliseconds)
//   * Elevation (shadow blur + offset_y, pixels)
//   * Theme (16-slot palette + 4-slot typography + spacing + motion +
//            elevation), built-in dark / light / high-contrast variants
//   * apply_brand_override(theme, brand) -- replaces primary while
//                                           keeping on_primary readable
//
// Out of scope (V3+): per-state palette (hover / pressed / disabled)
// will land as separate ColorToken arrays once the widget catalog
// settles on a per-state interaction model.
// =============================================================================
#pragma once

#include <cd/core/Defines.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cd::ui::theme
{

// ---- ColorToken ------------------------------------------------------------
//
// Float RGBA in the [0..1] range. Stored as float so downstream HDR /
// linear-space renderers don't have to round-trip through 8-bit sRGB.
// Default = opaque black; explicit-init the few fields you actually mean
// at construction.
struct ColorToken
{
    float r { 0.0F };
    float g { 0.0F };
    float b { 0.0F };
    float a { 1.0F };

    friend constexpr bool operator==(ColorToken, ColorToken) noexcept = default;
};

// Compute perceived luminance per Rec. 709 (Y' = 0.2126 R + 0.7152 G +
// 0.0722 B). Inputs assumed already linear; for sRGB inputs a callsite
// can pre-linearise. Used for the WCAG contrast helper.
[[nodiscard]] constexpr float relative_luminance(ColorToken c) noexcept
{
    return (0.2126F * c.r) + (0.7152F * c.g) + (0.0722F * c.b);
}

// WCAG 2.1 contrast ratio between two colors, range [1..21]. The formula
// is (L_lighter + 0.05) / (L_darker + 0.05). AAA body text requires >=7.
[[nodiscard]] constexpr float contrast_ratio(ColorToken a, ColorToken b) noexcept
{
    const float la = relative_luminance(a);
    const float lb = relative_luminance(b);
    const float lighter = (la > lb) ? la : lb;
    const float darker  = (la > lb) ? lb : la;
    return (lighter + 0.05F) / (darker + 0.05F);
}

// ---- Typography ------------------------------------------------------------
//
// font_name      -- font face identifier looked up by the font atlas.
// size_pt        -- nominal point size; renderer converts to pixels via
//                   the DPI scale at draw time.
// line_height    -- multiplier applied to size_pt (1.4 typical for body).
// letter_spacing -- tracking in em units (0 = font's natural metrics).
struct Typography
{
    std::string_view font_name     { "default" };
    float            size_pt       { 14.0F };
    float            line_height   { 1.4F };
    float            letter_spacing{ 0.0F };
};

// ---- Spacing ---------------------------------------------------------------
//
// Five-step semantic spacing scale, pixels @ 1.0 DPI scale. Matches the
// Material 3 / Tailwind 2 / Carbon pattern: callers pick the role name
// (xs / sm / md / lg / xl) rather than a literal pixel value, so the
// brand can rescale the whole UI by editing one struct.
struct Spacing
{
    float xs { 2.0F };
    float sm { 4.0F };
    float md { 8.0F };
    float lg { 16.0F };
    float xl { 32.0F };
};

// ---- Motion ----------------------------------------------------------------
//
// Three duration buckets, milliseconds. Material 3 calls these "short",
// "medium" and "long" emphasis levels. Callers map their easing /
// transition to the bucket that matches the affordance (button press =
// short, panel expand = medium, page transition = long).
struct Motion
{
    float duration_short  { 100.0F };
    float duration_medium { 250.0F };
    float duration_long   { 500.0F };
};

// ---- Elevation -------------------------------------------------------------
//
// Phase 1 elevation tokens: a single "active" pair of shadow blur and
// vertical offset, pixels. Elevation level 0 means "no shadow"; widgets
// that want no shadow can either pass an Elevation{0, 0} or check
// shadow_blur == 0.
struct Elevation
{
    float shadow_blur     { 4.0F };
    float shadow_offset_y { 2.0F };
};

// ---- Palette / typography slot enums --------------------------------------
//
// Sixteen-slot semantic palette (Material 3 roles). Indexing by enum
// makes lookups self-documenting and lets the compiler catch typos that
// a string-keyed map would not.
enum class PaletteSlot : std::uint8_t
{
    kPrimary           = 0,
    kOnPrimary         = 1,
    kPrimaryContainer  = 2,
    kOnPrimaryContainer= 3,
    kSecondary         = 4,
    kOnSecondary       = 5,
    kSurface           = 6,
    kOnSurface         = 7,
    kSurfaceVariant    = 8,
    kOnSurfaceVariant  = 9,
    kBackground        = 10,
    kOnBackground      = 11,
    kError             = 12,
    kOnError           = 13,
    kOutline           = 14,
    kInverseSurface    = 15,
};

inline constexpr std::size_t kPaletteSize = 16;

enum class TypographySlot : std::uint8_t
{
    kBody    = 0,
    kHeading = 1,
    kLabel   = 2,
    kCaption = 3,
};

inline constexpr std::size_t kTypographySize = 4;

// ---- Theme aggregate -------------------------------------------------------
//
// Bundle of all tokens. Pass-by-value friendly: 16 ColorTokens + 4
// Typography + 4 small POD structs ~= a few hundred bytes; widgets can
// hold a copy without ownership concerns.
struct Theme
{
    std::array<ColorToken, kPaletteSize>    palette;
    std::array<Typography, kTypographySize> typography;
    Spacing   spacing;
    Motion    motion;
    Elevation elevation;

    // Convenience accessors -- read by widgets so the [(std::size_t)slot]
    // ceremony stays out of widget code.
    [[nodiscard]] constexpr ColorToken color(PaletteSlot s) const noexcept
    {
        return palette[static_cast<std::size_t>(s)];
    }

    [[nodiscard]] constexpr Typography font(TypographySlot s) const noexcept
    {
        return typography[static_cast<std::size_t>(s)];
    }
};

// ---- Built-in themes -------------------------------------------------------
//
// Three flavours -- the production palette ships dark by default,
// light is the same chroma rotated to a high-luminance surface, and
// high-contrast hits WCAG AAA for body text (>=7:1) for users with
// low-vision accessibility requirements.
[[nodiscard]] Theme kDarkTheme() noexcept;
[[nodiscard]] Theme kLightTheme() noexcept;
[[nodiscard]] Theme kHighContrastTheme() noexcept;

// ---- Named palette factories (phase695 / M14 W6A) --------------------------
//
// Stable named entry points for the three canonical palettes. Prefer these
// over the kXxxTheme() functions in new call sites -- the `default_*_palette`
// names are consistent with Material 3 / design-system vocabulary and make
// a round-trip through a theme_name string (e.g. .cdproj) straightforward.
//
//   default_dark_palette()          -- warm-dark Material 3 reference palette
//   default_light_palette()         -- near-white surface, dark text
//   default_high_contrast_palette() -- black surface + white text (WCAG AAA)
[[nodiscard]] inline Theme default_dark_palette() noexcept
{
    return kDarkTheme();
}

[[nodiscard]] inline Theme default_light_palette() noexcept
{
    return kLightTheme();
}

[[nodiscard]] inline Theme default_high_contrast_palette() noexcept
{
    return kHighContrastTheme();
}

// Resolve a theme_name string (as stored in .cdproj) to one of the three
// built-in themes. Unknown strings fall back to the dark palette.
[[nodiscard]] inline Theme theme_from_name(std::string_view name) noexcept
{
    if (name == "light")         { return default_light_palette(); }
    if (name == "high_contrast") { return default_high_contrast_palette(); }
    return default_dark_palette();  // "dark" or any unknown value
}

// Canonical name strings for persistence (e.g. .cdproj theme_name field).
inline constexpr std::string_view kThemeNameDark          = "dark";
inline constexpr std::string_view kThemeNameLight         = "light";
inline constexpr std::string_view kThemeNameHighContrast  = "high_contrast";

// ---- Palette interpolation (phase715 / M16 W6) ----------------------------
//
// lerp_palette() blends every ColorToken field (r, g, b, a) linearly
// between `from` and `to` at parameter `t` in [0, 1].
//
//   t = 0.0  => returns `from` exactly.
//   t = 1.0  => returns `to`   exactly.
//   t = 0.5  => returns per-channel midpoint.
//
// Intended use: 200 ms cross-fade animation when the editor theme picker
// switches palettes. The non-palette fields (typography / spacing / motion
// / elevation) are taken from `to` so the new layout metrics apply
// immediately while only the colour tokens animate.
[[nodiscard]] inline Theme lerp_palette(const Theme& from,
                                        const Theme& to,
                                        float        t) noexcept
{
    Theme result = to;  // non-color fields (typo/spacing/motion/elevation) from `to`
    for (std::size_t i = 0; i < kPaletteSize; ++i)
    {
        const ColorToken& f = from.palette[i];
        const ColorToken& d = to.palette[i];
        result.palette[i] = ColorToken {
            f.r + (d.r - f.r) * t,
            f.g + (d.g - f.g) * t,
            f.b + (d.b - f.b) * t,
            f.a + (d.a - f.a) * t,
        };
    }
    return result;
}

// ---- Brand override --------------------------------------------------------
//
// Replace the primary swatch with `brand`, and re-pick on_primary so it
// keeps a readable contrast ratio against the new primary (white if the
// brand is dark, black if it is light). Other slots are untouched -- the
// caller can layer additional overrides on top.
[[nodiscard]] Theme apply_brand_override(Theme theme, ColorToken brand) noexcept;

}  // namespace cd::ui::theme
